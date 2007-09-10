/*
 * Block manager implementation - address ordered list.
 */

#include "blkman-ao.h"

/*
 * Insert block into list of free blocks.
 */

static void mb_insert(memblock_t *newblk, memblock_t *guard)
{
	mb_valid(newblk);
	mb_valid(guard);
	assert(mb_is_guard(guard));

	DEBUG("will insert block [$%.8x; %u; $%.2x] on free list\n", (uint32_t)newblk, newblk->size, newblk->flags);

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

	newblk->next->prev = newblk;

	mb_touch(blk->next);

	/* blk - block after which new block is inserted */
	blk->next = newblk;

	mb_touch(blk);

	DEBUG("inserted after block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
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

	if (mb_is_last(blk))
		newblk->flags |= MB_FLAG_LAST;

	mb_touch(newblk);

	/* shrink block and correct pointer */
	blk->next = newblk;
	blk->size = size;

	if (mb_is_last(blk))
		blk->flags &= ~MB_FLAG_LAST;

	mb_touch(blk);

	/* correct pointer in next block */
	mb_valid(newblk->next);

	newblk->next->prev = newblk;

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

	DEBUG("pulling out block [$%.8x, prev: $%.8x, next: $%.8x] from list\n",
		  (uint32_t)blk, (uint32_t)blk->prev, (uint32_t)blk->next);

	/* correct pointer in previous block */
	mb_valid(blk->prev);

	blk->prev->next = blk->next;

	mb_touch(blk->prev);

	/* correct pointer in next block */
	mb_valid(blk->next);

	blk->next->prev = blk->prev;

	mb_touch(blk->next);

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
	while (!mb_is_guard(blk)) {
		mb_valid(blk->next);

		if ((uint32_t)blk + blk->size != (uint32_t)blk->next)
			break;

		/* 'next' cannot be guard, because of condition above */
		memblock_t *next = blk->next;

		mb_pullout(next);

		blk->size += next->size;

		if (mb_is_last(next))
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);
	}

	/* coalesce with previous block */
	while (!mb_is_guard(blk)) {
		mb_valid(blk->prev);

		if ((uint32_t)blk->prev + blk->prev->size != (uint32_t)blk)
			break;

		/* 'blk' nor 'next' cannot be guard, because of condition above */
		memblock_t *next = blk;

		blk = blk->prev;

		mb_pullout(next);

		blk->size += next->size;

		if (mb_is_last(next))
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);
	}

	return blk;
}

/*
 * Print memory blocks and statistics.
 */

void mb_print(memblock_t *guard)
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

		if (mb_is_first(blk))
			mark = " : FIRST";
		if (mb_is_last(blk))
			mark = " : LAST";
		if (mb_is_first(blk) && mb_is_last(blk))
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
	fprintf(stderr, "\033[0;36mFirst free block: $%.8x, last free block: $%.8x.\033[0m\n", (uint32_t)guard->next, (uint32_t)guard->prev);
}

/*
 * Create initial block in given memory area.
 */

void mb_init(memblock_t *guard, uint32_t size)
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

void *mb_alloc(memblock_t *guard, uint32_t size, bool from_last)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	DEBUG("requested block of size %u\n", size);

	/* calculate block size */
	size = ALIGN(size + offsetof(memblock_t, next), MB_GRANULARITY);

	/* browse free blocks list */
	memblock_t *blk = (from_last) ? (guard->prev) : (guard->next);

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

void mb_free(memblock_t *guard, void *memory)
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

uint32_t mb_list_can_shrink(memblock_t *guard)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	return (mb_is_last(guard->prev)) ? SIZE_IN_PAGES(guard->prev->size - sizeof(memblock_t)) : 0;
}

void mb_list_shrink(memblock_t *guard, uint32_t pages)
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

void mb_list_expand(memblock_t *guard, uint32_t pages)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	assert(mb_is_guard(guard));

	assert(pages > 0);

	memblock_t *blk = guard->prev;

	if (mb_is_last(blk)) {
		blk->size += pages * PAGE_SIZE;

		mb_touch(blk);
	} else {
		memblock_t *newblk = (memblock_t *)((uint32_t)guard + guard->size);

		if (mb_is_guard(blk))
			blk = (memblock_t *)((uint32_t)blk + sizeof(memblock_t));

		/* OPTIMIZE: Unfortunately 'blk' is not last block, it must be found! */
		while ((uint32_t)blk + blk->size < (uint32_t)guard + guard->size) {
			mb_valid(blk);

			blk = (memblock_t *)((uint32_t)blk + blk->size);
		}

		assert(mb_is_last(blk));

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
	}

	guard->size += pages * PAGE_SIZE;

	mb_touch(guard);
}

/*
 * Merge two lists of memory blocks due to coalescing two memory areas.
 */

void mb_list_merge(memblock_t *first_guard, memblock_t *second_guard)
{
	mb_valid(first_guard);
	mb_valid(second_guard);

	/* a few checks */
	assert(mb_is_guard(first_guard));
	assert(mb_is_guard(second_guard));

	assert(first_guard < second_guard);

	/* check whether second free memory blocks list is empty */
	if (!mb_is_guard(second_guard->next)) {
		memblock_t *first_free = second_guard->next;

		/* find block to which append second memory blocks list */
		memblock_t *last_free = NULL;

		if (!mb_is_guard(first_guard->next)) {
			if (!mb_is_used(first_guard->prev)) {
				/* luckily, last block is free */
				last_free = first_guard->prev;
			} else {
				/* have to traverse whole list :( */
				last_free = first_guard->next;

				while (!mb_is_guard(last_free))
					last_free = last_free->next;
			}
		} else {
			/* if no free block on first list, second list will be appended to first_guard */
			last_free = first_guard;
		}

		/* at last merge lists */
		last_free->next = first_free;
		first_free->prev = last_free;

		mb_touch(last_free);
		mb_touch(first_free);

		/* WARNING: here is bug - last item for second list should point to first_guard!
		 * I'm tired of not having pointer to last free block! */
	}

	/* last block in first_guard is not valid anymore, mark it as ordinary
	 * block and copy pointer to last block from second_guard */
	first_guard->prev->flags &= ~MB_FLAG_LAST;
	first_guard->prev = second_guard->prev;

	mb_touch(first_guard);

	/* second_guard will be turned into degenerated free block (without storage
	 * are due to small size) and hopefully will be merged with another free block */
	memblock_t *newblk = second_guard;

	newblk->size  = sizeof(memblock_t);
	newblk->flags = 0;
	newblk->next  = NULL;
	newblk->prev  = NULL;

	mb_touch(newblk);

	mb_insert(newblk, first_guard);
	mb_coalesce(newblk);
}

/*
 * Find the largest free block that can split memory blocks list.
 */

uint32_t mb_list_can_split(memblock_t *guard, memblock_t **to_split)
{
}
