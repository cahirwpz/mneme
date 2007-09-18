/*
 * Block manager implementation - address ordered list.
 */

#include "blkman-ao.h"
#include <string.h>

/**
 * Insert block into list of free blocks.
 * @param list
 * @param newblk
 */

static void mb_insert(mb_list_t *list, mb_free_t *newblk)
{
	mb_valid(list);
	mb_valid(newblk);

	assert(mb_is_guard(list));
	assert(!mb_is_used(newblk) && !mb_is_guard(newblk));

	DEBUG("will insert block [$%.8x; %u; $%.2x] on free list\n", (uint32_t)newblk, newblk->size, newblk->flags);

	/* search the list for place where new block will be placed */
	mb_free_t *blk = (mb_free_t *)list;

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

static void mb_split(mb_list_t *list, mb_free_t **splitted, uint32_t size, bool second)
{
	mb_free_t *blk = *splitted;

	mb_valid(blk);

	assert(!mb_is_used(blk) && !mb_is_guard(blk));
	assert((size & MB_GRANULARITY_MASK) == 0);

	/* split the block if it is large enough */
	if (blk->size - size < sizeof(mb_free_t))
		return;

	/* calculate new block address */
	mb_free_t *newblk = (mb_free_t *)((uint32_t)blk + (second ? (blk->size - size) : size));

	/* initalize new block */
	newblk->size  = second ? size : (blk->size - size);
	newblk->flags = 0;
	newblk->prev  = blk;
	newblk->next  = blk->next;

	if (mb_is_last(blk))
		newblk->flags |= MB_FLAG_LAST;

	mb_touch(newblk);

	/* shrink block and correct pointer */
	blk->next = newblk;
	blk->size = second ? (blk->size - size) : size;

	if (mb_is_last(blk))
		blk->flags &= ~MB_FLAG_LAST;

	mb_touch(blk);

	/* correct pointer in next block */
	mb_valid(newblk->next);

	newblk->next->prev = newblk;

	mb_touch(newblk->next);

	/* increase blocks' counter */
	list->blkcnt++;
	list->fmemcnt -= sizeof(mb_t);

	mb_touch(list);

	DEBUG("splitted blocks: [$%.8x; %u; $%.2x] [$%.8x; %u; $%.2x]\n",
		  (uint32_t)blk, blk->size, blk->flags, (uint32_t)newblk, newblk->size, newblk->flags);

	*splitted = second ? newblk : blk;
}

/**
 * Pull out the block from list of free blocks.
 * @param blk
 */

static void mb_pullout(mb_free_t *blk)
{
	mb_valid(blk);

	assert(!mb_is_used(blk) && !mb_is_guard(blk));

	DEBUG("pulling out block [$%.8x; %u; $%.2x] [prev: $%.8x; next: $%.8x] from list\n",
		  (uint32_t)blk, blk->size, blk->flags, (uint32_t)blk->prev, (uint32_t)blk->next);

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

static mb_free_t *mb_coalesce(mb_list_t *list, mb_free_t *blk)
{
	mb_valid(blk);

	assert(!mb_is_used(blk) && !mb_is_guard(blk));

	/* coalesce with next block */
	while (!mb_is_guard(blk)) {
		mb_valid(blk->next);

		assert(!mb_is_used(blk));

		if ((uint32_t)blk + blk->size != (uint32_t)blk->next)
			break;

		/* 'next' cannot be guard, because of condition above */
		mb_free_t *next = blk->next;

		mb_pullout(next);

		blk->size += next->size;

		if (mb_is_last(next))
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);

		list->blkcnt--;
		list->fmemcnt += sizeof(mb_t);

		mb_touch(list);
	}

	/* coalesce with previous block */
	while (!mb_is_guard(blk)) {
		mb_valid(blk->prev);

		if ((uint32_t)blk->prev + blk->prev->size != (uint32_t)blk)
			break;

		/* 'blk' nor 'next' cannot be guard, because of condition above */
		mb_free_t *next = blk;

		blk = blk->prev;

		mb_pullout(next);

		blk->size += next->size;

		if (mb_is_last(next))
			blk->flags |= MB_FLAG_LAST;

		mb_touch(blk);

		list->blkcnt--;
		list->fmemcnt += sizeof(mb_t);
		
		mb_touch(list);
	}

#ifdef DEADMEMORY
	uint32_t *ptr = (uint32_t *)((uint32_t)blk + sizeof(mb_free_t));

	while (ptr < (uint32_t *)((uint32_t)blk + blk->size))
		*ptr++ = 0xDEADC0DE;
#endif

	return blk;
}

/**
 * Print memory blocks and statistics.
 * @param list
 */

void mb_print(mb_list_t *list)
{
	/* check if it is guard block */
	mb_valid(list);
	assert(mb_is_guard(list));

	/* find first block */
	mb_t *blk = (mb_t *)((uint32_t)list + sizeof(mb_list_t));

	fprintf(stderr, "\033[1;36mBlocks in range $%.8x - $%.8x:\033[0m\n", (uint32_t)blk, ((uint32_t)list + list->size));

	uint32_t used = 0, free = 0, largest = 0, free_blocks = 0, used_blocks = 0;

	bool error = FALSE;

	mb_free_t *first_free = (mb_free_t *)list, *last_free = (mb_free_t *)list;

	while ((uint32_t)blk < (uint32_t)list + list->size) {
		mb_valid(blk);

		if (!mb_is_used(blk)) {
			if (first_free == (mb_free_t *)list)
				first_free = last_free = (mb_free_t *)blk;
			else
				last_free = (mb_free_t *)blk;
		}

		fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x : %c%c : %5d",
				mb_is_used(blk) ? '1' : '2', (uint32_t)blk, (uint32_t)blk + blk->size,
				mb_is_first(blk) ? 'F' : '-', mb_is_last(blk) ? 'L' : '-', blk->size);

		if (mb_is_first(blk) && ((uint32_t)blk != (uint32_t)list + sizeof(mb_list_t)))
			error = TRUE;

		if (mb_is_last(blk) && ((uint32_t)blk + blk->size != (uint32_t)list + list->size))
			error = TRUE;

		if (!mb_is_used(blk)) {
			mb_free_t *fblk = (mb_free_t *)blk;

			fprintf(stderr, " : $%.8x $%8x", (uint32_t)fblk->prev, (uint32_t)fblk->next);
		}
		
		fprintf(stderr, "\033[0m\n");

		if (mb_is_used(blk)) {
			used += blk->size;
			used_blocks++;

			if (blk->size < sizeof(mb_t) + MB_GRANULARITY)
				error = TRUE;
		} else {
			free += blk->size - sizeof(mb_t);
			used += sizeof(mb_t);

			if (largest < blk->size)
				largest = blk->size;

			free_blocks++;
		}

		blk = (mb_t *)((uint32_t)blk + blk->size);
	}

	float fragmentation = (free != 0) ? ((float)(largest - sizeof(mb_t)) / (float)free) * 100.0 : 0.0;

	fprintf(stderr, "\033[1;36mSize: %d, Used: %d, Free: %d\033[0m\n", list->size, used, list->fmemcnt);
	fprintf(stderr, "\033[1;36mLargest free block: %d, Fragmentation: %.2f%%\033[0m\n", largest, fragmentation);
	fprintf(stderr, "\033[0;36mBlocks: %u, free blocks: %u, used blocks: %u.\033[0m\n", list->blkcnt, list->blkcnt - list->ublkcnt, list->ublkcnt);
	fprintf(stderr, "\033[0;36mFirst free block: $%.8x, last free block: $%.8x.\033[0m\n", (uint32_t)list->next, (uint32_t)list->prev);

	assert(list->blkcnt == used_blocks + free_blocks);
	assert(list->ublkcnt == used_blocks);
	assert(list->fmemcnt == free);
	assert(first_free == list->next);
	assert(last_free == list->prev);
	assert(error == FALSE);
}

/**
 * Create initial block in given memory area.
 * @param list
 * @param size
 */

void mb_init(mb_list_t *list, uint32_t size)
{
	/* first memory block to be managed */
	mb_free_t *blk = (mb_free_t *)((uint32_t)list + sizeof(mb_list_t));

	/* initialize guard block */
	list->prev  = blk;
	list->next  = blk;
	list->size  = size;
	list->flags = MB_FLAG_GUARD;

	list->fmemcnt = list->size - (sizeof(mb_t) + sizeof(mb_list_t));
	list->blkcnt  = 1;
	list->ublkcnt = 0;

	mb_touch(list);

	DEBUG("list guard [$%.8x; %u; $%.2x]\n", (uint32_t)list, list->size, list->flags);

	/* initialize first free block */
	blk->prev  = (mb_free_t *)list;
	blk->next  = (mb_free_t *)list;
	blk->size  = list->size - sizeof(mb_list_t);
	blk->flags = MB_FLAG_FIRST | MB_FLAG_LAST;

	mb_touch(blk);

	DEBUG("first block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
}

/**
 * Find block in given memory area and reserve it for use by caller.
 * @param list
 * @param size
 * @param from_last
 * @return 
 */

void *mb_alloc(mb_list_t *list, uint32_t size, bool from_last)
{
	/* check if it is guard block */
	mb_valid(list);

	assert(mb_is_guard(list));

	/* calculate block size */
	size = ALIGN(size + sizeof(mb_t), MB_GRANULARITY);

	/* browse free blocks list */
	mb_free_t *blk = (from_last) ? (list->prev) : (list->next);

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
	mb_split(list, &blk, size, mb_is_first(blk));

	/* block is ready to be pulled out of list */
	mb_pullout(blk);

	/* mark block as used */
	blk->flags |= MB_FLAG_USED;

	mb_touch(blk);

	/* increase amount of used blocks */
	list->ublkcnt++;
	list->fmemcnt -= blk->size - sizeof(mb_t);

	mb_touch(list);

	DEBUG("will use block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	return (void *)((uint32_t)blk + sizeof(mb_t));
}

/**
 * Find aligned block in given memory area and reserve it for use by caller.
 * @param list
 * @param size
 * @param alignment
 * @return 
 */

void *mb_alloc_aligned(mb_list_t *list, uint32_t size, uint32_t alignment)
{
	/* check if it is guard block */
	mb_valid(list);
	assert(mb_is_guard(list));

	if (alignment <= MB_GRANULARITY)
		return mb_alloc(list, size, FALSE);

	size = ALIGN(size, MB_GRANULARITY);

	/* browse free blocks list */
	uint32_t start = 0;
	uint32_t base  = 0;
	uint32_t end   = 0;

	mb_free_t *blk = list->next;

	while (TRUE) {
		mb_valid(blk);

		if (mb_is_guard(blk))
			return NULL;

		start = (uint32_t)blk;
		base  = ALIGN(start + sizeof(mb_t), alignment);
		end   = start + blk->size;

		if ((base + size <= end) &&
			((base - start == sizeof(mb_t)) ||
			 (base - start >= sizeof(mb_t) + sizeof(mb_free_t))))
			break;

		blk = blk->next;
	}

	/* now we're sure that we found place for our new aligned block,
	 * however work is not done, even two new block have to be created */

	DEBUG("will split block [$%.8x; %u; $%.2x] for use by aligned memory\n", (uint32_t)blk, blk->size, blk->flags);

	/* split block to create new block from unused space after aligned block */
	if (end - (base + size) >= sizeof(mb_free_t)) {
		mb_split(list, &blk, end - (base + size), TRUE);

		DEBUG("splitted 'after' block: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

		blk = blk->prev;
	}

	DEBUG("%d\n", (base - sizeof(mb_t)) - start);

	/* split block to create new block from unused space before aligned block */
	if (base - start >= sizeof(mb_t) + sizeof(mb_free_t)) {
		mb_split(list, &blk, (base - sizeof(mb_t)) - start, FALSE);

		DEBUG("splitted 'before' block: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

		blk = blk->next;
	}

	DEBUG("will use block [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	assert((uint32_t)blk + sizeof(mb_t) == ALIGN((uint32_t)blk + sizeof(mb_t), alignment));

	/* block is ready to be pulled out of list */
	mb_pullout(blk);

	/* mark block as used */
	blk->flags |= MB_FLAG_USED;

	mb_touch(blk);

	/* increase amount of used blocks */
	list->ublkcnt++;
	list->fmemcnt -= blk->size - sizeof(mb_t);

	mb_touch(list);

	return (void *)((uint32_t)blk + sizeof(mb_t));
}

/**
 * Resize given block.
 * @param list
 * @param memory
 * @param new_size
 * @return 
 */

bool mb_resize(mb_list_t *list, void *memory, uint32_t new_size)
{
	/* check if it is guard block */
	mb_valid(list);
	assert(mb_is_guard(list));
	assert(new_size > 0);

	mb_t *blk = (mb_t *)((uint32_t)memory - sizeof(mb_t));

	mb_valid(blk);

	uint32_t old_size = blk->size;

	new_size = ALIGN(new_size + sizeof(mb_t), MB_GRANULARITY);

	DEBUG("resizing block at $%.8x from %u to %u.\n", (uint32_t)blk, old_size, new_size);

	/* is resizing really needed ? */
	if (old_size == new_size)
		return TRUE;

	/* find next block */
	mb_t *next = NULL;

	if ((uint32_t)blk + blk->size < (uint32_t)list + list->size) {
		next = (mb_t *)((uint32_t)blk + blk->size);
	
		DEBUG("found next block [$%.8x; %u; $%.2x]\n", (uint32_t)next, next->size, next->flags);
	}

	if (old_size > new_size) {
		/* shrinking block */

		if (old_size - new_size <= sizeof(mb_free_t))
			return TRUE;

		/* resize block */
		blk->size = new_size;
		mb_touch(blk);

		/* create new free block from leftovers */
		mb_free_t *new = (mb_free_t *)((uint32_t)blk + new_size);

		new->flags = 0;
		
		if (!next) {
			new->flags |= MB_FLAG_LAST;
			blk->flags &= ~MB_FLAG_LAST;
			
			mb_touch(blk);
		}

		new->size  = old_size - new_size;
		new->prev  = NULL;
		new->next  = NULL;
		mb_touch(new);

		mb_insert(list, new);

		list->fmemcnt += new->size - sizeof(mb_t);
		list->blkcnt  += 1;
		mb_touch(list);

		if (next && !mb_is_used(next))
			mb_coalesce(list, new);
	} else {
		/* expanding block */

		if (!next || mb_is_used(next) || (old_size + next->size <= new_size))
			return FALSE;

		uint32_t diff = new_size - old_size;

		DEBUG("expanding block at $%.8x by %u bytes.\n", (uint32_t)blk, diff);

		DEBUG("next_size %u; diff %u.\n", next->size, diff);

		if (next->size - diff < sizeof(mb_free_t)) {
			if (mb_is_last(next))
				blk->flags |= MB_FLAG_LAST;

			blk->size += next->size;

			mb_touch(blk);

			mb_pullout((mb_free_t *)next);

			list->blkcnt--;
			list->fmemcnt -= next->size - sizeof(mb_t);

			mb_touch(list);
		} else {
			mb_free_t *moved = (mb_free_t *)((uint32_t)blk + new_size);

			DEBUG("moving block $%.8x to $%.8x.\n", (uint32_t)next, (uint32_t)moved);

			moved->prev  = ((mb_free_t *)next)->prev;
			moved->next  = ((mb_free_t *)next)->next;

			moved->size  = next->size - diff;
			moved->flags = next->flags;

			moved->prev->next = moved;
			moved->next->prev = moved;

			mb_touch(moved->prev);
			mb_touch(moved->next);

			mb_touch(moved);

			DEBUG("moved block [$%.8x; %u; $%.2x]\n", (uint32_t)moved, moved->size, moved->flags);

			blk->size = new_size;

			mb_touch(blk);

			list->fmemcnt -= diff;

			mb_touch(list);
		}
	}

	return FALSE;
}

/**
 * Free block in memory area reffered by given pointer.
 * @param list
 * @param memory
 * @return
 */

mb_free_t *mb_free(mb_list_t *list, void *memory)
{
	/* check if it is guard block */
	mb_valid(list);
	assert(mb_is_guard(list));

	mb_t *blk = (mb_t *)((uint32_t)memory - sizeof(mb_t));

	mb_valid(blk);

	DEBUG("requested to free block at $%.8x\n", (uint32_t)blk);

	/* mark block as free */
	mb_free_t *fblk = (mb_free_t *)blk;

	fblk->flags &= ~MB_FLAG_USED;
	fblk->prev   = NULL;
	fblk->next   = NULL;

	mb_touch(fblk);

	/* insert on free list and coalesce */
	mb_insert(list, fblk);

	/* decrease amount of free blocks */
	list->ublkcnt--;
	list->fmemcnt += fblk->size - sizeof(mb_t);

	mb_touch(list);
	
	return mb_coalesce(list, fblk);
}

/**
 * Find last block in area managed by block manager.
 * @param guard
 * @return 
 */

static mb_t *mb_list_find_last(mb_list_t *list)
{
	mb_valid(list);

	assert(mb_is_guard(list));

	mb_t *blk = (mb_t *)list->prev;

	if (!mb_is_last(blk)) {
		if (mb_is_guard(blk))
			blk = (mb_t *)((uint32_t)blk + sizeof(mb_list_t));

		/* OPTIMIZE: Unfortunately 'blk' is not last block, it must be found! */
		while ((uint32_t)blk + blk->size < (uint32_t)list + list->size) {
			mb_valid(blk);

			blk = (mb_t *)((uint32_t)blk + blk->size);
		}

		assert(mb_is_last(blk));
	}

	DEBUG("last block in list at $%.8x is: [$%.8x; %u; $%.2x]\n", (uint32_t)list, (uint32_t)blk, blk->size, blk->flags);

	return blk;
}

/**
 * Check if there is unused room at the end of blocks' list.
 * @param list
 * @return
 */

uint32_t mb_list_can_shrink_at_end(mb_list_t *list)
{
	mb_valid(list);
	mb_valid(list->prev);

	assert(mb_is_guard(list));

	return (mb_is_last(list->prev)) ? (list->prev->size - sizeof(mb_free_t)) / PAGE_SIZE : 0;
}

/**
 * Check if there is unused room at the beginning of blocks' list.
 * @param list
 * @param space
 * @return
 */

uint32_t mb_list_can_shrink_at_beginning(mb_list_t *list, uint32_t space)
{
	mb_valid(list);
	mb_valid(list->next);

	if (!mb_is_first(list->next))
		return 0;
	
	int32_t pages	 = list->next->size / PAGE_SIZE;
	int32_t leftover = list->next->size - pages * PAGE_SIZE;

	if (pages == 0)
		return 0;

	return (leftover > 0 && leftover < sizeof(mb_free_t)) ? pages - 1 : pages;
}

/**
 * Shrink blocks' list from the end.
 * @param list
 * @param pages
 */

void mb_list_shrink_at_end(mb_list_t *list, uint32_t pages)
{
	mb_valid(list);
	assert(pages > 0);
	assert(mb_is_guard(list));

	DEBUG("will shrink list of blocks at $%.8x from right side by %u pages\n", (uint32_t)list, pages);

	mb_valid(list->prev);
	assert(mb_is_last(list->prev));
	assert(list->prev->size - pages * PAGE_SIZE >= sizeof(mb_free_t));

	list->prev->size -= pages * PAGE_SIZE;

	mb_touch(list->prev);

	list->size    -= pages * PAGE_SIZE;
	list->fmemcnt -= pages * PAGE_SIZE;

	mb_touch(list);
}

/**
 * Shrink blocks' list from the beginning.
 * @param to_shrink
 * @param pages
 * @param space
 */

void mb_list_shrink_at_beginning(mb_list_t **to_shrink, uint32_t pages, uint32_t space)
{
	mb_list_t *list    = *to_shrink;
	mb_list_t *newlist = NULL;

	mb_valid(list);
	assert(mb_is_guard(list));
	assert(pages > 0);
	
	DEBUG("will shrink list of blocks at $%.8x from left side by %u pages\n", (uint32_t)list, pages);

	mb_valid(list->next);
	assert(mb_is_first(list->next));
	
	/* Two posibilities: first free block can be removed or shrinked */
	assert(list->next->size - pages * PAGE_SIZE >= 0);

	if (list->next->size - pages * PAGE_SIZE == 0) {
		/* remove first block */
		mb_pullout(list->next);

		newlist = (mb_list_t *)((uint32_t)list + pages * PAGE_SIZE);

		/* copy data to new guard block and correct pointers */
		newlist->size    = list->size - pages * PAGE_SIZE;
		newlist->flags   = list->flags;
		newlist->blkcnt  = list->blkcnt - 1;
		newlist->ublkcnt = list->ublkcnt;
		newlist->fmemcnt = list->fmemcnt - pages * PAGE_SIZE + sizeof(mb_t);

		if ((mb_free_t *)list == list->next) {
			newlist->prev = (mb_free_t *)newlist;
			newlist->next = (mb_free_t *)newlist;
		} else {
			newlist->prev		= list->prev;
			newlist->next		= list->next;
			newlist->prev->next = (mb_free_t *)newlist;
			newlist->next->prev = (mb_free_t *)newlist;

			mb_touch(newlist->prev);
			mb_touch(newlist->next);
		}

		mb_touch(newlist);

		/* mark the block after newlist as MB_FLAG_FIRST */
		mb_t *blk = (mb_t *)((uint32_t)newlist + sizeof(mb_list_t));
		blk->flags |= MB_FLAG_FIRST;
		mb_touch(blk);
	} else {
		newlist = (mb_list_t *)((uint32_t)list + pages * PAGE_SIZE);

		mb_free_t *newfirst = (mb_free_t *)((uint32_t)list->next + pages * PAGE_SIZE);

		/* copy list and first block in new place */
		memcpy(newlist, list, sizeof(mb_list_t) + sizeof(mb_free_t));

		/* correct pointers in newlist and its neighbours */
		newlist->size		-= pages * PAGE_SIZE;
		newlist->next		 = newfirst;
		newlist->prev->next  = (mb_free_t *)newlist;
		newlist->fmemcnt	-= pages * PAGE_SIZE;

		if (list->next == list->prev) {
			newlist->prev       = newfirst;
			newlist->next->next = (mb_free_t *)newlist;
		}

		mb_touch(newlist->prev);
		mb_touch(newlist);

		/* correct pointers in newfirst and its neighbours */
		newfirst->size		-= pages * PAGE_SIZE;
		newfirst->prev		 = (mb_free_t *)newlist;
		newfirst->next->prev = newfirst;

		mb_touch(newfirst);
		mb_touch(newfirst->next);
	}

	DEBUG("new list: [$%.8x; %u; %.2x] [prev: $%.8x; next: $%.8x]\n",
		  (uint32_t)newlist, newlist->size, newlist->flags, (uint32_t)newlist->prev, (uint32_t)newlist->next);
	DEBUG("new first block: [$%.8x; %u; $%.2x] [prev: $%.8x; next: $%.8x]\n",
		  (uint32_t)newlist->next, newlist->next->size, newlist->next->flags,
		  (uint32_t)newlist->next->prev, (uint32_t)newlist->next->next);

	*to_shrink = newlist;
}

/**
 * Extend sbrk memory area with 'pages' number of pages.
 * @param list
 * @param pages
 */

void mb_list_expand(mb_list_t *list, uint32_t pages)
{
	mb_valid(list);
	assert(mb_is_guard(list));
	assert(pages > 0);

	DEBUG("will expand list of block at $%.8x by %u pages\n", (uint32_t)list, pages);

	mb_t *blk = mb_list_find_last(list);

	if (mb_is_used(blk)) {
		mb_free_t *newblk = (mb_free_t *)((uint32_t)list + list->size);

		blk->flags &= ~MB_FLAG_LAST;

		mb_touch(blk);

		/* setup new block */
		newblk->size	= pages * PAGE_SIZE;
		newblk->flags	= MB_FLAG_LAST;
		newblk->prev	= NULL;
		newblk->next	= NULL;

		mb_touch(newblk);

		/* insert new block into list of free blocks */
		mb_insert(list, newblk);

		list->blkcnt  += 1;
		list->fmemcnt -= sizeof(mb_t);
	} else {
		blk->size += pages * PAGE_SIZE;

		mb_touch(blk);
	}

	list->size    += pages * PAGE_SIZE;
	list->fmemcnt += pages * PAGE_SIZE;

	mb_touch(list);
}

/**
 * Merge two lists of memory blocks due to coalescing two memory areas.
 * @param first
 * @param second
 * @param space
 */

void mb_list_merge(mb_list_t *first, mb_list_t *second, uint32_t space)
{
	mb_valid(first);
	mb_valid(second);

	DEBUG("will merge following lists: [$%.8x; %u; $%.2x; %u; %u; %u] [$%.8x; %u; $%.2x; %u; %u; %u]\n",
		  (uint32_t)first, first->size, first->flags, first->blkcnt, first->ublkcnt, first->fmemcnt,
		  (uint32_t)second, second->size, second->flags, second->blkcnt, second->ublkcnt, second->fmemcnt);

	/* a few checks */
	assert(mb_is_guard(first));
	assert(mb_is_guard(second));

	assert(((uint32_t)first + first->size) == ((uint32_t)second - space));
	assert(space >= sizeof(mb_free_t));

	/* last block in first memory blocks' list is not last in joined list */
	mb_free_t *blk;

	blk = (mb_free_t *)mb_list_find_last(first);
	blk->flags &= ~MB_FLAG_LAST;
	mb_touch(blk);

	/* first block in second memory blocks' list is not first in joined list */
	blk = (mb_free_t *)((uint32_t)second + sizeof(mb_list_t));
	blk->flags &= ~MB_FLAG_FIRST;
	mb_touch(blk);

	/* sum size of two lists */
	first->size	   += second->size + space;
	first->blkcnt  += second->blkcnt;
	first->ublkcnt += second->ublkcnt;
	first->fmemcnt += second->fmemcnt;

	/* turn second guard into ordinary free block and increase its size by 'space' */
	blk = (mb_free_t *)((uint32_t)second - space);

	blk->flags = 0;
	blk->size  = sizeof(mb_list_t) + space;

	first->blkcnt++;
	first->fmemcnt += blk->size - sizeof(mb_t);
	
	if ((mb_free_t *)second == second->next) {
		blk->next = blk;
		blk->prev = blk;
	} else {
		blk->prev = second->prev;
		blk->next = second->next;

		blk->prev->next = blk;
		blk->next->prev = blk;

		mb_touch(blk->next);
	}

	mb_free_t *last = first->prev;

	blk->prev->next = (mb_free_t *)first;
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

	mb_coalesce(first, blk);

	DEBUG("merged into: [$%.8x; %u; $%.2x; %u; %u]\n",
		  (uint32_t)first, first->size, first->flags, first->blkcnt, first->ublkcnt);
}

/**
 * Find the first free block that can split memory blocks list,
 * next search can start from block next to returned block.
 * @param list
 * @param to_split
 * @param cut
 * @param space
 * @return
 */

uint32_t mb_list_find_split(mb_list_t *list, mb_free_t **to_split, void **cut, uint32_t space)
{
	mb_valid(list);
	assert(mb_is_guard(list));
	assert(to_split != NULL && cut != NULL);

	mb_free_t *blk  = list->next;

	DEBUG("start searching for split-block from: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);

	uint32_t pages	   = 0;
	uint32_t end	   = 0;
	uint32_t cut_point = 0;
	uint32_t end_point = 0;

	space     += sizeof(mb_list_t);
	*to_split = NULL;

	/* browse free blocks list */
	while (TRUE) {
		mb_valid(blk);

		if (mb_is_guard(blk))
			break;

		if (!mb_is_first(blk)) {
			end       = (uint32_t)blk + blk->size;
			cut_point = ALIGN_UP((uint32_t)blk + sizeof(mb_free_t), PAGE_SIZE);
			end_point = ALIGN_DOWN(end - space, PAGE_SIZE);

			if (cut_point < end_point) {
				/* found at least one aligned free page :) */
				pages = (end_point - cut_point) / PAGE_SIZE;

				int32_t leftover = (end - end_point) - space;

				assert(leftover >= 0);

				if (leftover > 0 && leftover < sizeof(mb_free_t))
					pages--;

				if (pages > 0) {
					*to_split = blk;
					*cut	  = (void *)cut_point;

					break;
				}
			}
		}

		blk = blk->next;
	}

	if (pages > 0) {
		DEBUG("split-block found: [$%.8x; %u; $%.2x], will cut [$%.8x; $%x]\n",
			  (uint32_t)blk, blk->size, blk->flags, (uint32_t)cut_point, pages * PAGE_SIZE);
	} else {
		DEBUG("split-block not found\n");
	}

	return pages;
}

/**
 * Recalculate statistics.
 * @param list
 */

static void mb_list_recalculate_statistics(mb_list_t *list)
{
	/* check if it is guard block */
	mb_valid(list);
	assert(mb_is_guard(list));

	/* find first block */
	mb_t *blk = (mb_t *)((uint32_t)list + sizeof(mb_list_t));

	uint32_t used_blocks = 0, blocks = 0, free = 0;

	while ((uint32_t)blk < (uint32_t)list + list->size) {
		mb_valid(blk);

		if (mb_is_used(blk))
			used_blocks++;
		else
			free += blk->size - sizeof(mb_t);

		blocks++;

		blk = (mb_t *)((uint32_t)blk + blk->size);
	}

	list->fmemcnt = free;
	list->ublkcnt = used_blocks;
	list->blkcnt  = blocks;

	mb_touch(list);
}

/**
 * Split list of memory blocks using 'to_split' block. Return guard of second list.
 * @param first
 * @param to_split
 * @param pages
 * @param space
 * @return
 */

mb_list_t *mb_list_split(mb_list_t *first, mb_free_t *to_split, uint32_t pages, uint32_t space)
{
	mb_valid(first);
	assert(mb_is_guard(first));

	mb_valid(to_split);
	assert(!mb_is_guard(to_split) && !mb_is_used(to_split));

	DEBUG("split block's list [$%.8x; %u; $%.2x] at block [$%.8x; %u; $%.2x] removing %u pages\n",
		  (uint32_t)first, first->size, first->flags, (uint32_t)to_split, to_split->size, to_split->flags, pages);

	uint32_t cut_start = ALIGN((uint32_t)to_split + sizeof(mb_free_t), PAGE_SIZE);
	uint32_t cut_end   = cut_start + pages * PAGE_SIZE;

	/* set up guard of second list */
	mb_list_t *second = (mb_list_t *)(cut_end + space);

	second->flags = MB_FLAG_GUARD;
	second->size  = ((uint32_t)first + first->size) - (cut_end + space);
	second->next  = mb_is_guard(to_split->next) ? (mb_free_t *)second : to_split->next;
	second->prev  = (first->prev == to_split) ? (mb_free_t *)second : first->prev;

	mb_touch(second);

	/* correct pointers in second guard neighbours */
	second->next->prev = (mb_free_t *)second;
	second->prev->next = (mb_free_t *)second;

	mb_touch(second->next);
	mb_touch(second->prev);

	/* check if there should be a leftover at the beginning of second */
	mb_free_t *blk = (mb_free_t *)((uint32_t)second + sizeof(mb_list_t));

	uint32_t size = (uint32_t)to_split + to_split->size - (uint32_t)blk;

	if (size > 0) {
		blk->size  = size;
		blk->flags = MB_FLAG_FIRST;

		mb_touch(blk);

		mb_insert(second, blk);
	} else {
		/* mark first block of second list with MB_FLAG_FIRST */
		blk->flags |= MB_FLAG_FIRST;

		mb_touch(blk);
	}

	/* now correct first list */
	first->prev = to_split;
	first->size = cut_start - (uint32_t)first;

	mb_touch(first);

	/* propely finish first list */
	to_split->flags |= MB_FLAG_LAST;
	to_split->next = (mb_free_t *)first;
	to_split->size = cut_start - (uint32_t)to_split;

	mb_touch(to_split);

	/* recalculate statistics */
	mb_list_recalculate_statistics(first);
	mb_list_recalculate_statistics(second);

	return second;
}
