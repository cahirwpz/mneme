/*
 * Block manager implementation - address ordered list.
 */

#include "blkman-ao.h"
#include <string.h>

/**
 * Insert block into list of free blocks.
 * @param newblk
 * @param guard
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

/**
 * Split block in two blocks. First will be of length "size".
 * @param blk
 * @param size
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

/**
 * Pull out the block from list of free blocks.
 * @param blk
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

/**
 * Coalesce free block with adhering free blocks.
 * @param blk
 * @return 
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

	{
		uint32_t *ptr = (uint32_t *)((uint32_t)blk + sizeof(memblock_t));
		
		while (ptr < (uint32_t *)((uint32_t)blk + blk->size))
			*ptr++ = 0xDEADC0DE;
	}

	return blk;
}

/**
 * Print memory blocks and statistics.
 * @param guard
 */

void mb_print(memblock_t *guard)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

	/* find first block */
	memblock_t *blk = guard + 1;

	fprintf(stderr, "\033[1;36mBlocks in range $%.8x - $%.8x:\033[0m\n", (uint32_t)blk, ((uint32_t)guard + guard->size));

	uint32_t used = 0, free = 0, largest = 0;

	bool error = FALSE;

	memblock_t *first_free = guard, *last_free = guard;

	while ((uint32_t)blk < (uint32_t)guard + guard->size) {
		mb_valid(blk);

		if (!mb_is_used(blk)) {
			if (first_free == guard)
				first_free = last_free = blk;
			else
				last_free = blk;
		}

		const char *mark = "";

		if (mb_is_first(blk)) {
			mark = " : FIRST";
			if ((uint32_t)blk != (uint32_t)guard + sizeof(memblock_t))
				error = TRUE;
		}

		if (mb_is_last(blk)) {
			mark = " : LAST";
			if ((uint32_t)blk + blk->size != (uint32_t)guard + guard->size)
				error = TRUE;
		}

		if (mb_is_first(blk) && mb_is_last(blk))
			mark = " : FIRST and LAST";

		if (mb_is_used(blk)) {
			fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x : %d%s\033[0m\n", mb_is_used(blk) ? '1' : '2',
					(uint32_t)blk, ((uint32_t)blk + blk->size - 1), blk->size, mark);
		} else {
			fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x : %d%s : $%.8x $%8x\033[0m\n",
					mb_is_used(blk) ? '1' : '2', (uint32_t)blk, ((uint32_t)blk + blk->size),
					blk->size, mark, (uint32_t)blk->prev, (uint32_t)blk->next);
		}

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

	assert(first_free == guard->next);
	assert(last_free == guard->prev);
	assert(error == FALSE);
}

/**
 * Create initial block in given memory area.
 * @param guard
 * @param size
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

/**
 * Find block in given memory area and reserve it for use by caller.
 * @param guard
 * @param size
 * @param from_last
 * @return 
 */

void *mb_alloc(memblock_t *guard, uint32_t size, bool from_last)
{
	/* check if it is guard block */
	mb_valid(guard);
	assert(mb_is_guard(guard));

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

/**
 * Free block in memory area reffered by given pointer.
 * @param guard
 * @param memory
 */

memblock_t *mb_free(memblock_t *guard, void *memory)
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
	
	return mb_coalesce(blk);
}

/**
 * Find last block in area managed by block manager.
 * @param guard
 * @return 
 */

static memblock_t *mb_find_last(memblock_t *guard)
{
	mb_valid(guard);
	assert(mb_is_guard(guard));

	memblock_t *blk = guard->prev;

	if (!mb_is_last(blk)) {
		if (mb_is_guard(blk))
			blk = (memblock_t *)((uint32_t)blk + sizeof(memblock_t));

		/* OPTIMIZE: Unfortunately 'blk' is not last block, it must be found! */
		while ((uint32_t)blk + blk->size < (uint32_t)guard + guard->size) {
			mb_valid(blk);

			blk = (memblock_t *)((uint32_t)blk + blk->size);
		}

		assert(mb_is_last(blk));
	}

	DEBUG("last block in list at $%.8x is: [$%.8x; %u; $%.2x]\n", (uint32_t)guard, (uint32_t)blk, blk->size, blk->flags);

	return blk;
}

/**
 * Check if there is unused room at the end of blocks' list.
 * @param guard
 * @return
 */

uint32_t mb_list_can_shrink_at_end(memblock_t *guard)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	return (mb_is_last(guard->prev)) ? (guard->prev->size - sizeof(memblock_t)) / PAGE_SIZE : 0;
}

/**
 * Check if there is unused room at the beginning of blocks' list.
 * @param guard
 * @param space
 * @return
 */

uint32_t mb_list_can_shrink_at_beginning(memblock_t *guard, uint32_t space)
{
	mb_valid(guard);
	mb_valid(guard->next);

	return (mb_is_first(guard->next)) ? guard->next->size / PAGE_SIZE : 0;
}

/**
 * Shrink blocks' list from the end.
 * @param guard
 * @param pages
 */

void mb_list_shrink_at_end(memblock_t *guard, uint32_t pages)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	DEBUG("will shrink list of blocks at $%.8x from right side by %u pages\n", (uint32_t)guard, pages);

	assert(mb_is_last(guard->prev));
	assert(pages > 0);
	assert(guard->prev->size - pages * PAGE_SIZE >= sizeof(memblock_t));

	guard->prev->size -= pages * PAGE_SIZE;

	mb_touch(guard->prev);

	guard->size -= pages * PAGE_SIZE;

	mb_touch(guard);
}

/**
 * Shrink blocks' list from the beginning.
 * @param guard
 * @param pages
 * @param space
 */

void mb_list_shrink_at_beginning(memblock_t **to_shrink, uint32_t pages, uint32_t space)
{
	memblock_t *guard	 = *to_shrink;
	memblock_t *newguard = NULL;

	mb_valid(guard);
	mb_valid(guard->next);
	assert(mb_is_guard(guard));

	DEBUG("will shrink list of blocks at $%.8x from left side by %u pages\n", (uint32_t)guard, pages);

	assert(mb_is_first(guard->next));
	assert(pages > 0);
	
	/* Two posibilities: first free block can be removed or shrinked */
	assert(guard->next->size - pages * PAGE_SIZE >= 0);

	if (guard->next->size - pages * PAGE_SIZE == 0) {
		/* remove first block */
		mb_pullout(guard->next);

		newguard = (memblock_t *)((uint32_t)guard + pages * PAGE_SIZE);

		/* copy data to new guard block and correct pointers */
		newguard->size		 = guard->size - pages * PAGE_SIZE;
		newguard->flags		 = guard->flags;

		if (guard == guard->next) {
			newguard->prev = newguard;
			newguard->next = newguard;
		} else {
			newguard->prev = guard->prev;
			newguard->next = guard->next;
			newguard->prev->next = newguard;
			newguard->next->prev = newguard;

			mb_touch(newguard->prev);
			mb_touch(newguard->next);
		}

		mb_touch(newguard);

		/* mark the block after newguard as MB_FLAG_FIRST */
		memblock_t *blk = (memblock_t *)((uint32_t)newguard + sizeof(memblock_t));
		blk->flags |= MB_FLAG_FIRST;
		mb_touch(blk);
	} else {
		newguard = (memblock_t *)((uint32_t)guard + pages * PAGE_SIZE);

		memblock_t *newfirst = (memblock_t *)((uint32_t)guard->next + pages * PAGE_SIZE);

		/* copy guard and first block in new place */
		memcpy(newguard, guard, sizeof(memblock_t) + sizeof(memblock_t));

		/* correct pointers in newguard and its neighbours */
		newguard->size		-= pages * PAGE_SIZE;
		newguard->next		 = newfirst;
		newguard->prev->next = newguard;

		if (guard->next == guard->prev) {
			newguard->prev       = newfirst;
			newguard->next->next = newguard;
		}

		mb_touch(newguard->prev);
		mb_touch(newguard);

		/* correct pointers in newfirst and its neighbours */
		newfirst->size		-= pages * PAGE_SIZE;
		newfirst->prev		 = newguard;
		newfirst->next->prev = newfirst;

		mb_touch(newfirst);
		mb_touch(newfirst->next);
	}

	DEBUG("new guard: [$%.8x; %u; %.2x] [prev: $%.8x; next: $%.8x]\n",
		  (uint32_t)newguard, newguard->size, newguard->flags, (uint32_t)newguard->prev, (uint32_t)newguard->next);
	DEBUG("new first block: [$%.8x; %u; $%.2x] [prev: $%.8x; next: $%.8x]\n",
		  (uint32_t)newguard->next, newguard->next->size, newguard->next->flags,
		  (uint32_t)newguard->next->prev, (uint32_t)newguard->next->next);

	*to_shrink = newguard;
}

/*
 * Extend sbrk memory area with 'pages' number of pages.
 */

void mb_list_expand(memblock_t *guard, uint32_t pages)
{
	mb_valid(guard);
	mb_valid(guard->prev);

	DEBUG("will expand list of block at $%.8x by %u pages\n", (uint32_t)guard, pages);

	assert(mb_is_guard(guard));

	assert(pages > 0);

	memblock_t *blk = mb_find_last(guard);

	if (mb_is_used(blk)) {
		memblock_t *newblk = (memblock_t *)((uint32_t)guard + guard->size);

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

/*
 * Merge two lists of memory blocks due to coalescing two memory areas.
 */

void mb_list_merge(memblock_t *first, memblock_t *second, uint32_t space)
{
	memblock_t *blk;

	mb_valid(first);
	mb_valid(second);

	DEBUG("will merge two lists of blocks: [$%.8x; %u; $%.2x] and [$%.8x; %u; $%.2x]\n",
		  (uint32_t)first, first->size, first->flags, (uint32_t)second, second->size, second->flags);

	/* a few checks */
	assert(mb_is_guard(first));
	assert(mb_is_guard(second));

	assert(((uint32_t)first + first->size) == ((uint32_t)second - space));
	assert(space >= sizeof(memblock_t));

	/* last block in first memory blocks' list is not last in joined list */
	blk = mb_find_last(first);
	blk->flags &= ~MB_FLAG_LAST;
	mb_touch(blk);

	/* first block in second memory blocks' list is not first in joined list */
	blk = (memblock_t *)((uint32_t)second + sizeof(memblock_t));
	blk->flags &= ~MB_FLAG_FIRST;
	mb_touch(blk);

	/* sum size of two lists */
	first->size	+= second->size + space;

	/* turn second guard into ordinary free block and increase its size by 'space' */
	blk = (memblock_t *)((uint32_t)second - space);

	blk->flags = 0;
	blk->size  = sizeof(memblock_t) + space;
	
	if (second == second->next) {
		blk->next   = blk;
		blk->prev   = blk;
	} else {
		blk->prev = second->prev;
		blk->next = second->next;

		blk->prev->next = blk;
		blk->next->prev = blk;

		mb_touch(blk->next);
	}

	memblock_t *last = first->prev;

	blk->prev->next = first;
	first->prev 	= blk->prev;
	blk->prev 		= last;
	last->next		= blk;

	DEBUG("first: [$%.8x; %u; $%.2x]\n", (uint32_t)first, first->size, first->flags);
	DEBUG("last:  [$%.8x; %u; $%.2x]\n", (uint32_t)last, last->size, last->flags);
	DEBUG("blk:   [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	mb_touch(first);
	mb_touch(first->prev);
	mb_touch(last);
	mb_touch(blk);

	mb_coalesce(blk);

	DEBUG("merged into: [$%.8x; %u; $%.2x]\n", (uint32_t)first, first->size, first->flags);
}

/*
 * Find the first free block that can split memory blocks list,
 * next search can start from block next to returned block.
 */

memblock_t *mb_list_find_split(memblock_t *blk, uint32_t *offset, uint32_t *pages, uint32_t space)
{
	mb_valid(blk);

	if (mb_is_guard(blk))
		blk = blk->next;

	*pages = 0;

	DEBUG("start searching for split-block from: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	/* browse free blocks list */
	while (TRUE) {
		mb_valid(blk);

		if (mb_is_guard(blk))
			break;

		uint32_t cut_point = mb_is_first(blk) ? ((uint32_t)blk - sizeof(memblock_t) - space) : ALIGN((uint32_t)blk + sizeof(memblock_t), PAGE_SIZE);
		uint32_t end_point = (uint32_t)blk + blk->size - (space + sizeof(memblock_t));

		if ((cut_point < end_point) && (cut_point + PAGE_SIZE <= end_point)) {
			/* found at least one aligned free page :) */
			*pages  = SIZE_IN_PAGES(end_point - cut_point);
			*offset = cut_point - (uint32_t)blk; 

			break;
		}

		blk = blk->next;
	}

	if (*pages > 0) {
		DEBUG("split-block found: [$%.8x; %u; $%.2x], will cut [$%.8x; $%x]\n",
			  (uint32_t)blk, blk->size, blk->flags, (uint32_t)blk + *offset, *pages * PAGE_SIZE);
	} else {
		DEBUG("split-block not found\n");
	}

	return blk;
}

/*
 * Split list of memory blocks using 'to_split' block. Return guard of second list.
 */

memblock_t *mb_list_split(memblock_t *first, memblock_t *to_split, uint32_t pages, uint32_t space)
{
	mb_valid(first);
	mb_valid(to_split);
	assert(mb_is_guard(first));
	assert(!mb_is_guard(to_split));
	assert(!mb_is_used(to_split));

	uint32_t cut_start = ALIGN((uint32_t)first + sizeof(memblock_t), PAGE_SIZE);
	uint32_t cut_end   = cut_start + pages * PAGE_SIZE;

	/* set up guard of second list */
	memblock_t *second = (memblock_t *)(cut_end + space);

	second->flags = MB_FLAG_FIRST;
	second->size  = first->size - (cut_end + space);
	second->next  = mb_is_guard(to_split->next) ? second : to_split->next;
	second->prev  = (first->prev == to_split) ? second : first->prev;

	mb_touch(second);

	/* correct pointers in second guard neighbours */
	second->next->prev = second;

	mb_touch(second->next);

	second->prev->next = second;

	mb_touch(second->prev);

	/* mark first block of second list with MB_FLAG_FIRST */
	memblock_t *blk = (memblock_t *)((uint32_t)second + sizeof(memblock_t));

	blk->flags |= MB_FLAG_FIRST;

	mb_touch(blk);

	/* now correct first list */
	first->prev = to_split;

	mb_touch(first);

	/* propely finish first list */
	to_split->flags |= MB_FLAG_LAST;
	to_split->next = first;
	to_split->size = cut_start - (uint32_t)to_split;

	mb_touch(to_split);

	return second;
}
