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

/*
 * Memory block structure
 */

struct memblock_used
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;
};

typedef struct memblock_used memblock_used_t;

struct memblock
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;

	struct memblock *next;
	struct memblock *prev;
};

typedef struct memblock memblock_t;

/*
 * Calculate checksum of memory block structure.
 */

static uint16_t mb_checksum(memblock_t *blk)
{
	int bytes = ((blk->flags & MB_FLAG_USED) ? sizeof(memblock_used_t) : sizeof(memblock_t)) - sizeof(uint16_t);

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
 * Print memory blocks and statistics.
 */

void ma_print(memarea_t *area)
{
	memblock_t *blk = (memblock_t *)ALIGN((uint32_t)(area + 1), MB_GRANULARITY);

	printf("\033[1;36mMemory area %p - %p:\033[0m\n", blk, (void *)((uint32_t)area + area->size - 1));

	uint32_t used = 0, free = 0, largest = 0;

	while ((uint32_t)blk < (uint32_t)area + area->size) {
		mb_valid(blk);

		const char *mark = "";

		if (blk->flags & MB_FLAG_FIRST)
			mark = " : FIRST";
		if (blk->flags & MB_FLAG_LAST)
			mark = " : LAST";
		if ((blk->flags & MB_FLAG_FIRST) && (blk->flags & MB_FLAG_LAST))
			mark = " : FIRST and LAST";

		printf("\033[1;3%cm  $%.8x - $%.8x : %d%s\033[0m\n", (blk->flags & MB_FLAG_USED) ? '1' : '2',
				(uint32_t)blk, ((uint32_t)blk + blk->size - 1), blk->size, mark);

		if (blk->flags & MB_FLAG_USED) {
			used += blk->size;
		} else {
			free += blk->size;

			if (largest < blk->size)
				largest = blk->size;
		}

		blk = (memblock_t *)((uint32_t)blk + blk->size);
	}

	printf("\033[1;36mSize: %d, Used: %d, Free: %d\033[0m\n", area->size, used, free);
	printf("\033[1;36mLargest free block: %d, Fragmentation: %.2f%%\033[0m\n", largest, (1.0 - (float)largest / (float)free) * 100.0);
}

/*
 * Create initial block in given memory area.
 */

static void mb_init(memarea_t *area)
{
	ma_valid(area);

	memblock_t *blk = area->free;

	blk->prev 	  = NULL;
	blk->next 	  = NULL;
	blk->size 	  = area->size - ((uint32_t)area->free - (uint32_t)area);
	blk->checksum = mb_checksum(blk);
	blk->flags	  = MB_FLAG_FIRST | MB_FLAG_LAST;

	mb_touch(blk);

	DEBUG("initial block [$%.8x; %u; $%x]\n", (uint32_t)blk, blk->size, blk->flags);
}

/*
 * Insert block into list of free blocks.
 */

static void mb_insert(memblock_t *blk, memarea_t *area)
{
	DEBUG("will insert block [$%.8x; %u; $%x] on free list\n", (uint32_t)blk, blk->size, blk->flags);

	ma_valid(area);
	
	/* list is empty - insert at the beginning */
	if (area->free == NULL) {
		blk->next = NULL;
		blk->prev = NULL;

		area->free = blk;

		mb_touch(blk);
		ma_touch(area);

		return;
	}

	/* check whether to insert before first block */
	if (area->free > blk) {
		blk->next = area->free;
		blk->prev = NULL;

		mb_touch(blk);
		
		area->free->prev = blk;
		area->free 		 = blk;

		mb_touch(blk->next);
		ma_touch(area);

		return;
	}

	/* search the list for place where new block will be placed */
	memblock_t *currblk = area->free;

	/* iterate till next block exists and has earlier address */
	while (TRUE) {
		assert(currblk != blk);

		mb_valid(currblk);

		if ((currblk->next == NULL) || (currblk->next > blk))
			break;

		currblk = currblk->next;
	}

	/* blk - block being inserted */
	blk->next	= currblk->next;
	blk->prev	= currblk;

	mb_touch(blk);

	/* blk->next - block before which new block is inserted */
	if (blk->next) {
		mb_valid(blk->next);
		
		blk->next->prev = blk;

		mb_touch(blk->next);
	}

	/* currblk - block after which new block is inserted */
	currblk->next = blk;

	mb_touch(currblk);

	DEBUG("inserted after block [$%.8x; %u; $%x]\n", (uint32_t)currblk, currblk->size, currblk->flags);
}

/*
 * Split block in two blocks. First will be of length "size".
 */

static void mb_split(memblock_t *blk, uint32_t size, memarea_t *area)
{
	mb_valid(blk);
	assert((size & MB_GRANULARITY_MASK) == 0);

	/* split the block if it is large enough */
	if (blk->size == size)
		return;

	DEBUG("will split block at $%.8x\n", (uint32_t)blk);

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

	/* correct pointer in next block */
	if (newblk->next) {
		mb_valid(newblk->next);

		newblk->next->prev = newblk;
		
		mb_touch(newblk->next);
	}

	DEBUG("splitted blocks: [$%.8x; %u; $%x] [$%.8x; %u; $%x]\n",
		  (uint32_t)blk, blk->size, blk->flags, (uint32_t)newblk, newblk->size, newblk->flags);
}

/*
 * Pull out the block from list of free blocks.
 */

static void mb_pullout(memblock_t *blk, memarea_t *area)
{
	DEBUG("pulling out from free list block $%.8x, prev: $%.8x, next: $%.8x\n",
		  (uint32_t)blk, (uint32_t)blk->prev, (uint32_t)blk->next);

	ma_valid(area);

	if (blk->prev != NULL) {
		mb_valid(blk->prev);

		blk->prev->next = blk->next;

		mb_touch(blk->prev);
	} else {
		area->free = blk->next;
		
		ma_touch(area);
	}

	if (blk->next != NULL) {
		mb_valid(blk->next);

		blk->next->prev = blk->prev;

		mb_touch(blk->next);
	}

	blk->next = NULL;
	blk->prev = NULL;

	mb_touch(blk);
}

/*
 * Coalesce free block with adhering free blocks.
 */

static memblock_t *mb_coalesce(memblock_t *blk, memarea_t *area)
{
	ma_valid(area);

	/* coalesce with next block */
	while (blk->next) {
		if ((uint32_t)blk + blk->size != (uint32_t)blk->next)
			break;

		memblock_t *next = blk->next;

		mb_pullout(next, area);

		blk->size += next->size;

		if (next->flags & MB_FLAG_LAST)
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);
	}

	/* coalesce with previous block */
	while (blk->prev) {
		if ((uint32_t)blk->prev + blk->prev->size != (uint32_t)blk)
			break;

		blk = blk->prev;

		memblock_t *next = blk->next;

		mb_pullout(next, area);

		blk->size += next->size;

		if (next->flags & MB_FLAG_LAST)
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);
	}

	return blk;
}

/*
 * Find block in given memory area and reserve it for use by caller.
 */

static void *ma_alloc(memarea_t *area, uint32_t size)
{
	DEBUG("requested block of size %u\n", size);

	ma_valid(area);

	/* calculate block size */
	size = ALIGN(size + sizeof(memblock_used_t), MB_GRANULARITY);

	/* browse free blocks list */
	memblock_t *blk = area->free;

	while (blk != NULL) {
		mb_valid(blk);

		if (blk->size >= size)
			break;

		blk = blk->next;
	}

	if (blk == NULL)
		return blk;

	DEBUG("found block [$%.8x; %u; $%x]\n", (uint32_t)blk, blk->size, blk->flags);
	
	/* try to split block and save the rest on the list */
	mb_split(blk, size, area);

	/* block is ready to be pulled out of list */
	mb_pullout(blk, area);

	/* mark block as used */
	blk->flags |= MB_FLAG_USED;

	mb_touch(blk);

	DEBUG("will use block [$%.8x; %u; $%x]\n", (uint32_t)blk, blk->size, blk->flags);

	return (void *)((uint32_t)blk + sizeof(memblock_used_t));
}

#if 0
static void *ma_aligned_alloc(memarea_t *area, uint32_t size, uint32_t alignment)
{
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

static void ma_free(memarea_t *area, void *memory)
{
	memblock_t *blk = (memblock_t *)((uint32_t)memory - sizeof(memblock_used_t));

	DEBUG("requested to free $%.8x\n", (uint32_t)blk);

	mb_valid(blk);

	/* mark block as free */
	blk->flags &= ~MB_FLAG_USED;
	blk->prev   = NULL;
	blk->next   = NULL;

	mb_touch(blk);

	/* insert on free list and coalesce */
	mb_insert(blk, area);
	mb_coalesce(blk, area);
}

/* ========================================================================= */

void *mm_alloc(memarea_t *mm, uint32_t size)
{
	void *memory = NULL;
		
	memarea_t *area = mm;

	ma_valid(area);

	if (!(area->flags & MA_FLAG_READY)) {
		area->free = (memblock_t *)ALIGN((uint32_t)(area + 1), MB_GRANULARITY);
		area->flags |= MA_FLAG_READY;
		ma_touch(area);

		mb_init(area);
	}

	if ((memory = ma_alloc(area, size)))
		return memory;

	/* try to get memory from system if block not found */
	int n = SIZE_IN_PAGES(size);

	DEBUG("get %d pages from system\n", n);

	memblock_t *blk = pm_sbrk_alloc(n);

	if (blk == NULL) {
		DEBUG("cannot get memory from system\n");
		return NULL;
	}

	/* setup new block */
	blk->size	= n * PAGE_SIZE;
	blk->flags	= MB_FLAG_FIRST | MB_FLAG_LAST;
	blk->prev	= NULL;
	blk->next	= NULL;

	mb_touch(blk);

	/* insert new block into list of free blocks and coalesce it */
	mb_insert(blk, area);

	blk = mb_coalesce(blk, area);

	area->size += n * PAGE_SIZE;

	ma_touch(area);

	return ma_alloc(area, size);
}

void mm_free(memarea_t *mm, void *memory)
{
	ma_free(mm, memory);
}
