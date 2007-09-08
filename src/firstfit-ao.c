#include "common.h"
#include "sysmem.h"
#include "memmgr.h"

#include <stdio.h>

#define MB_GRANULARITY_BITS		4
#define MB_GRANULARITY			(1 << MB_GRANULARITY_BITS)
#define MB_GRANULARITY_MASK		(MB_GRANULARITY - 1)

#define MB_FLAG_USED	1
#define MB_FLAG_PAD		2
#define MB_FLAG_FIRST	4
#define MB_FLAG_LAST	8
#define MB_FLAG_GUARD	16

/*
 * Memory block structure
 */

struct memblock
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;

	struct memblock *next;	/* if (mb_is_guard(blk)) 'next' points to first free block or is loopback pointer */
	struct memblock *prev;	/* if (mb_is_guard(blk)) 'prev' points always to last block */
} __attribute__((aligned(16)));

typedef struct memblock memblock_t;

/*
 * Few inlines to make code more readable :)
 */

static inline bool mb_is_guard(memblock_t *blk) {
	return (blk->flags & MB_FLAG_GUARD);
}

static inline bool mb_is_used(memblock_t *blk) {
	return (blk->flags & MB_FLAG_USED);
}

static inline bool mb_is_last(memblock_t *blk) {
	return (blk->flags & MB_FLAG_LAST);
}

static inline memblock_t *mb_from_memarea(memarea_t *area) {
	return (memblock_t *)ALIGN((uint32_t)(area + 1), MB_GRANULARITY);
}

/*
 * Calculate checksum of memory block structure.
 */

static uint16_t mb_checksum(memblock_t *blk)
{
	int bytes = (mb_is_used(blk) ? offsetof(memblock_t, next) : sizeof(memblock_t)) - sizeof(uint16_t);

	return (((uint32_t)blk) >> 16) ^ (((uint32_t)blk) & 0xFFFF) ^ checksum((uint16_t *)&blk->flags, bytes >> 1);
}

/*
 * Recalculate memory block checksum.
 */

static inline void mb_touch(memblock_t *blk)
{
	blk->checksum = mb_checksum(blk);
}

/*
 * Check corectness of memory block checksum.
 */

static inline void mb_valid(memblock_t *blk)
{
	if (mb_checksum(blk) != blk->checksum) {
		fprintf(stderr, "invalid block: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
		abort();
	}
}

/*
 * Insert block into list of free blocks.
 */

static void mb_insert(memblock_t *newblk, memblock_t *guard)
{
	DEBUG("will insert block [$%.8x; %u; $%x] on free list\n", (uint32_t)newblk, newblk->size, newblk->flags);

	mb_valid(newblk);
	mb_valid(guard);

	/* search the list for place where new block will be placed */
	memblock_t *blk = guard;

	/* iterate till next block exists and has earlier address */
	while (TRUE) {
		assert(blk != newblk);

		mb_valid(blk);

		if (mb_is_guard(blk->next) || (blk->next > newblk))
			break;

		blk = blk->next;
	}

	/* newblk - block being inserted */
	newblk->next = blk->next;
	newblk->prev = blk;

	mb_touch(newblk);

	/* newblk->next - block before which new block is inserted */
	mb_valid(newblk->next);

	if (mb_is_guard(newblk->next)) {
		if (newblk > newblk->next->prev)
			newblk->next->prev = newblk;
	} else {
		blk->next->prev = newblk;
	}

	mb_touch(blk->next);

	/* currblk - block after which new block is inserted */
	blk->next = newblk;

	mb_touch(blk);

	DEBUG("inserted after block [$%.8x; %u; $%x]\n", (uint32_t)blk, blk->size, blk->flags);
}

/*
 * Split block in two blocks. First will be of length "size".
 */

static void mb_split(memblock_t *blk, uint32_t size)
{
	mb_valid(blk);

	assert(!mb_is_guard(blk));
	assert((size & MB_GRANULARITY_MASK) == 0);

	/* split the block if it is large enough */
	if (blk->size == size)
		return;

	/* DEBUG("will split block at $%.8x\n", (uint32_t)blk); */

	/* calculate new block address */
	memblock_t *newblk = (memblock_t *)((uint32_t)blk + size);

	/* initalize new block */
	newblk->size  = blk->size - size;
	newblk->flags = 0;
	newblk->prev  = blk;
	newblk->next  = blk->next;

	if (blk->flags & MB_FLAG_LAST)
		newblk->flags |= MB_FLAG_LAST;

	mb_touch(newblk);

	/* shrink block and correct pointer */
	blk->next = newblk;
	blk->size = size;

	if (blk->flags & MB_FLAG_LAST)
		blk->flags &= ~MB_FLAG_LAST;

	mb_touch(blk);

	mb_valid(newblk->next);

	/* correct pointer in next block */
	if (mb_is_guard(newblk->next)) {
		if (newblk > newblk->next->prev)
			newblk->next->prev = newblk;
	} else {
		newblk->next->prev = newblk;
	}

	mb_touch(newblk->next);

	DEBUG("splitted blocks: [$%.8x; %u; $%.2x] [$%.8x; %u; $%.2x]\n",
		  (uint32_t)blk, blk->size, blk->flags, (uint32_t)newblk, newblk->size, newblk->flags);
}

/*
 * Pull out the block from list of free blocks.
 */

static void mb_pullout(memblock_t *blk)
{
	mb_valid(blk);

	assert(!mb_is_guard(blk));

	DEBUG("pulling out from free list block $%.8x, prev: $%.8x, next: $%.8x\n",
		  (uint32_t)blk, (uint32_t)blk->prev, (uint32_t)blk->next);

	/* correct pointer in previous block */
	mb_valid(blk->prev);

	blk->prev->next = blk->next;

	mb_touch(blk->prev);

	/* correct pointer in next block */
	mb_valid(blk->next);

	if (!mb_is_guard(blk->next)) {
		blk->next->prev = blk->prev;

		mb_touch(blk->next);
	}

	/* clear pointers in block being pulled out */
	blk->next = NULL;
	blk->prev = NULL;

	mb_touch(blk);
}

/*
 * Coalesce free block with adhering free blocks.
 */

static memblock_t *mb_coalesce(memblock_t *blk)
{
	mb_valid(blk);

	/* coalesce with next block */
	while (blk->next) {
		if ((uint32_t)blk + blk->size != (uint32_t)blk->next)
			break;

		memblock_t *next = blk->next;

		mb_pullout(next);

		blk->size += next->size;

		if (next->flags & MB_FLAG_LAST)
			blk->flags |= MB_FLAG_LAST;

		if (mb_is_guard(blk->next) && (blk->next->prev == next)) {
			blk->next->prev = blk;

			mb_touch(blk->next);
		}

		mb_touch(blk);
	}

	/* coalesce with previous block */
	while (blk->prev) {
		if ((uint32_t)blk->prev + blk->prev->size != (uint32_t)blk)
			break;

		blk = blk->prev;

		memblock_t *next = blk->next;

		mb_pullout(next);

		blk->size += next->size;

		if (next->flags & MB_FLAG_LAST)
			blk->flags |= MB_FLAG_LAST;

		if (mb_is_guard(blk->next) && (blk->next->prev == next)) {
			blk->next->prev = blk;

			mb_touch(blk->next);
		}

		mb_touch(blk);
	}

	return blk;
}

/*
 * Print memory blocks and statistics.
 */

static void mb_print(memblock_t *guard)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	/* find first block */
	memblock_t *blk = guard + 1;

	fprintf(stderr, "\033[1;36mBlocks in range $%.8x - $%.8x:\033[0m\n", (uint32_t)blk, ((uint32_t)guard + guard->size - 1));

	uint32_t used = 0, free = 0, largest = 0;

	while ((uint32_t)blk < (uint32_t)guard + guard->size) {
		mb_valid(blk);

		const char *mark = "";

		if (blk->flags & MB_FLAG_FIRST)
			mark = " : FIRST";
		if (blk->flags & MB_FLAG_LAST)
			mark = " : LAST";
		if ((blk->flags & MB_FLAG_FIRST) && (blk->flags & MB_FLAG_LAST))
			mark = " : FIRST and LAST";

		fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x : %d%s\033[0m\n", mb_is_used(blk) ? '1' : '2',
				(uint32_t)blk, ((uint32_t)blk + blk->size - 1), blk->size, mark);

		if (mb_is_used(blk)) {
			used += blk->size;
		} else {
			free += blk->size;

			if (largest < blk->size)
				largest = blk->size;
		}

		blk = (memblock_t *)((uint32_t)blk + blk->size);
	}

	fprintf(stderr, "\033[1;36mSize: %d, Used: %d, Free: %d\033[0m\n", guard->size, used, free);
	fprintf(stderr, "\033[1;36mLargest free block: %d, Fragmentation: %.2f%%\033[0m\n", largest, (1.0 - (float)largest / (float)free) * 100.0);
	fprintf(stderr, "\033[0;36mFirst free block: $%.8x, last block: $%.8x.\033[0m\n", (uint32_t)guard->next, (uint32_t)guard->prev);
}

/*
 * Create initial block in given memory area.
 */

static void mb_init(memblock_t *guard, uint32_t size)
{
	/* first memory block to be managed */
	memblock_t *blk = guard + 1;

	/* initialize guard block */
	guard->prev  = blk;
	guard->next  = blk;
	guard->size  = size;
	guard->flags = MB_FLAG_GUARD;

	mb_touch(guard);

	DEBUG("guard block [$%.8x; %u; $%.2x]\n", (uint32_t)guard, guard->size, guard->flags);

	/* initialize first free block */
	blk->prev 	  = guard;
	blk->next 	  = guard;
	blk->size 	  = guard->size - sizeof(memblock_t);
	blk->flags	  = MB_FLAG_FIRST | MB_FLAG_LAST;

	mb_touch(blk);

	DEBUG("first block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
}

/*
 * Find block in given memory area and reserve it for use by caller.
 */

static void *mb_alloc(memblock_t *guard, uint32_t size, bool from_last)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	DEBUG("requested block of size %u\n", size);

	/* calculate block size */
	size = ALIGN(size + offsetof(memblock_t, next), MB_GRANULARITY);

	/* browse free blocks list */
	memblock_t *blk = (from_last) ? (guard->prev) : (guard->next);

	if (from_last && mb_is_used(blk))
		return NULL;

	while (TRUE) {
		mb_valid(blk);

		if (mb_is_guard(blk))
			return NULL;
		
		if (blk->size >= size)
			break;

		blk = blk->next;
	}

	if (blk == NULL)
		return blk;

	DEBUG("found block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
	
	/* try to split block and save the rest on the list */
	mb_split(blk, size);

	/* block is ready to be pulled out of list */
	mb_pullout(blk);

	/* mark block as used */
	blk->flags |= MB_FLAG_USED;

	mb_touch(blk);

	DEBUG("will use block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	return (void *)((uint32_t)blk + offsetof(memblock_t, next));
}

#if 0
static void *mb_aligned_alloc(memblock_t *guard, uint32_t size, uint32_t alignment)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	if (alignment < MB_GRANULARITY)
		return ma_alloc(area, size);

	/* browse free blocks list */
	memblock_t *blk = area->free;

	while (blk != NULL) {
		mb_valid(blk);

		if (blk->size >= size)
			break;

		blk = blk->next;
	}
}
#endif

/*
 * Free block in memory area reffered by given pointer.
 */

static void mb_free(memblock_t *guard, void *memory)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	memblock_t *blk = (memblock_t *)((uint32_t)memory - offsetof(memblock_t, next));

	mb_valid(blk);

	DEBUG("requested to free block at $%.8x\n", (uint32_t)blk);

	/* mark block as free */
	blk->flags &= ~MB_FLAG_USED;
	blk->prev   = NULL;
	blk->next   = NULL;

	mb_touch(blk);

	/* insert on free list and coalesce */
	mb_insert(blk, guard);
	mb_coalesce(blk);
}

/*
 * Shrink last memory block if it's not used.
 */

static uint32_t mb_can_shrink(memblock_t *guard)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	assert(mb_is_last(guard->prev));

	return (!mb_is_used(guard->prev)) ? SIZE_IN_PAGES(guard->prev->size - sizeof(memblock_t)) : 0;
}

static void mb_shrink(memblock_t *guard, uint32_t pages)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	assert(mb_is_last(guard->prev));
	assert(pages > 0);
	assert(guard->prev->size - pages * PAGE_SIZE > sizeof(memblock_t));

	guard->prev->size -= pages * PAGE_SIZE;

	mb_touch(guard->prev);

	guard->size -= pages * PAGE_SIZE;

	mb_touch(guard);
}

/*
 * Extend sbrk memory area with 'pages' number of pages.
 */

static void mb_expand(memblock_t *guard, uint32_t pages)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	assert(mb_is_last(guard->prev));
	assert(pages > 0);

	memblock_t *blk = guard->prev;

	if (mb_is_used(blk)) {
		memblock_t *newblk = (memblock_t *)((uint32_t)blk + blk->size);

		blk->flags &= ~MB_FLAG_LAST;

		mb_touch(blk);

		/* setup new block */
		newblk->size	= pages * PAGE_SIZE;
		newblk->flags	= MB_FLAG_LAST;
		newblk->prev	= NULL;
		newblk->next	= NULL;

		mb_touch(newblk);

		/* insert new block into list of free blocks */
		mb_insert(newblk, guard);
	} else {
		blk->size += pages * PAGE_SIZE;

		mb_touch(blk);
	}

	guard->size += pages * PAGE_SIZE;

	mb_touch(guard);
}

/* ========================================================================= */

void *mm_alloc(memmgr_t *mm, uint32_t size)
{
	memarea_t *area = mm->areas;

	ma_valid(area);

	memblock_t *guard = mb_from_memarea(area);

	if (!(area->flags & MA_FLAG_READY)) {
		mb_init(guard, area->size - ((uint32_t)guard - (uint32_t)area));

		area->flags |= MA_FLAG_READY;
		ma_touch(area);
	} 

	mb_valid(guard);

	void *memory = NULL;

	if (!(memory = mb_alloc(guard, size, FALSE)))
		if (ma_expand(area, SIZE_IN_PAGES(size))) {
			mb_expand(guard, SIZE_IN_PAGES(size));
			memory = mb_alloc(guard, size, TRUE);
		}

	return memory;
}

void mm_free(memmgr_t *mm, void *memory)
{
	memarea_t *area = mm->areas;

	mb_free(mb_from_memarea(area), memory);

	int32_t pages = mb_can_shrink(mb_from_memarea(area)) - 3;

	if ((pages > 0) && ma_shrink(area, pages))
		mb_shrink(mb_from_memarea(area), pages);
}

/*
 * Print memory areas.
 */

void mm_print(memmgr_t *mm)
{
	memarea_t *area = mm->areas;

	while (area != NULL) {
		ma_valid(area);

		mb_print(mb_from_memarea(area));

		area = area->next;
	}
}

