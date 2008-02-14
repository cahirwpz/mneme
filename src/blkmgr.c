/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	
 */

#include "blklst-ao.h"
#include "blkmgr.h"

/**
 * Memory manager initialization.
 * @param blkmgr
 * @param areamgr
 */

void blkmgr_init(blkmgr_t *blkmgr, areamgr_t *areamgr)/*{{{*/
{
	arealst_init(&blkmgr->blklst);

	blkmgr->areamgr = areamgr;
}/*}}}*/

/**
 * Memory block allocation procedure.
 * @param mm
 * @param size
 * @param alignment
 * @return
 */

void *blkmgr_alloc(blkmgr_t *blkmgr, uint32_t size, uint32_t alignment)/*{{{*/
{
	arealst_rdlock(&blkmgr->blklst);

	void *memory = NULL;

	if (alignment) {
		DEBUG("\033[37;1mRequested block of size %u aligned to %u bytes boundary.\033[0m\n", size, alignment);
	} else {
		DEBUG("\033[37;1mRequested block of size %u.\033[0m\n", size);
	}

	area_t *area = (area_t *)blkmgr->blklst.local.next;

	while (!area_is_guard(area)) {
		mb_list_t *list = mb_list_from_memarea(area);

		DEBUG("searching for free block in [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

		if (!area_is_ready(area)) {
			mb_init(list, area->size - sizeof(area_t));
			area->flags |= AREA_FLAG_READY;
			area_touch(area);
		} 

		memory = alignment ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);

		if (memory)
			break;

		area = area->local.next;
	}

	arealst_unlock(&blkmgr->blklst);

	if (memory)
		return memory;

	DEBUG("Not enough memory - trying to find adjacent free areas to merge with.\n");
	
	DEBUG("Not enough memory - trying to get some from area manager.\n");

	uint32_t area_size = size + sizeof(area_t) + sizeof(mb_list_t) + sizeof(mb_t);

	if (alignment)
		area_size += alignment;

	area_t *newarea = areamgr_alloc_area(blkmgr->areamgr, SIZE_IN_PAGES(area_size));

	if (!newarea)
		return NULL;

	mb_list_t *list = mb_list_from_memarea(newarea);

	mb_init(list, newarea->size - sizeof(area_t));

	newarea->flags |= AREA_FLAG_READY;
	area_touch(newarea);

	/* area is ready - we can try to merge it with adjacent areas from blklst */

	bool merged = FALSE;

	/* holy crap! double write locking is necessary :( */
	arealst_wrlock(&blkmgr->areamgr->global);
	arealst_wrlock(&blkmgr->blklst);

	/* get neighbours */
	area_t *prev = newarea->global.prev;
	area_t *next = newarea->global.next;

	/* check if neighbours are on managed list */
	if (arealst_has_area(&blkmgr->blklst, prev, DONTLOCK) && (area_end(prev) == area_begining(newarea))) {
		mb_list_t *to_merge = mb_list_from_memarea(prev);

		newarea = arealst_join_area(&blkmgr->areamgr->global, prev, newarea, DONTLOCK);

		list = mb_list_merge(to_merge, list, sizeof(area_t));

		merged = TRUE;
	}

	if (arealst_has_area(&blkmgr->blklst, next, DONTLOCK) && (area_end(newarea) == area_begining(next))) {
		mb_list_t *to_merge = mb_list_from_memarea(next);

		newarea = arealst_join_area(&blkmgr->areamgr->global, newarea, next, DONTLOCK);

		list = mb_list_merge(list, to_merge, sizeof(area_t));

		merged = TRUE;
	}

	arealst_unlock(&blkmgr->blklst);
	arealst_unlock(&blkmgr->areamgr->global);

	memory = alignment ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);

	/* If not merged then just insert */
	if (!merged)
		arealst_insert_area_by_addr(&blkmgr->blklst, (void *)newarea, LOCK);

	return memory;
}/*}}}*/

/**
 * Resizing allocated block procedure.
 * @param
 * @param
 * @return
 */

bool blkmgr_realloc(blkmgr_t *blkmgr, void *memory, uint32_t new_size)/*{{{*/
{
	DEBUG("\033[37;1mResizing block at $%.8x to %u bytes.\033[0m\n", (uint32_t)memory, new_size);

	bool result = FALSE;

	arealst_wrlock(&blkmgr->blklst);

	area_t *area = arealst_find_area_by_addr(&blkmgr->blklst, memory, DONTLOCK);

	if (area)
		result = mb_resize(mb_list_from_memarea(area), memory, new_size);

	arealst_unlock(&blkmgr->blklst);

	return result;
}/*}}}*/

/**
 * Memory block deallocation procedure.
 * @param mm
 * @param memory
 */

bool blkmgr_free(blkmgr_t *blkmgr, void *memory)/*{{{*/
{
	DEBUG("\033[37;1mRequested to free block at $%.8x.\033[0m\n", (uint32_t)memory);

	/* define actions on area */
	void     *cut_addr = NULL;
	uint32_t cut_pages = 0;

	uint32_t shrink_left_pages  = 0;
	uint32_t shrink_right_pages = 0;

	bool result = FALSE;

	arealst_wrlock(&blkmgr->blklst);
	
	area_t *area = arealst_find_area_by_addr(&blkmgr->blklst, memory, DONTLOCK);

	if (area) {
		mb_list_t *list = mb_list_from_memarea(area);
		mb_free_t *free = mb_free(list, memory);

		result = TRUE;

		uint32_t pages;

		/* is area completely empty (has exactly one block and it's free) */
		if ((blkmgr->blklst.areacnt > 1) && mb_is_first(list->next) && mb_is_last(list->next)) {
			arealst_remove_area(&blkmgr->blklst, (void *)area, DONTLOCK);
		} else {
			if (((list->blkcnt - list->ublkcnt) >= 4 * PAGE_SIZE) && (list->ublkcnt < (list->blkcnt / 2))) {
				/* can area be shrinked at the end ? */
				shrink_right_pages = mb_list_can_shrink_at_end(list);

				if (shrink_right_pages > 0)
					mb_list_shrink_at_end(list, pages);

				/* can area be shrinked at the beginning ? */
				shrink_left_pages = mb_list_can_shrink_at_beginning(list, sizeof(area_t));

				if (shrink_left_pages > 0)
					mb_list_shrink_at_beginning(&list, pages, sizeof(area_t));

				/* can area be splitted ? */
#if 0
				cut_pages = mb_list_find_split(list, &free, &cut_addr, sizeof(area_t));

				if (cut_pages > 0)
					mb_list_split(mb_list_from_memarea(area), free, pages, sizeof(area_t));
#endif
			}
		}
	}

	arealst_unlock(&blkmgr->blklst);

	if (shrink_right_pages)
		areamgr_shrink_area(blkmgr->areamgr, &area, shrink_right_pages, RIGHT);

	if (shrink_left_pages)
		areamgr_shrink_area(blkmgr->areamgr, &area, shrink_left_pages, LEFT);

	if (cut_pages) {
	}

	return result;
}/*}}}*/

/*
 * Print memory areas contents in given memory manager.
 */

void blkmgr_print(blkmgr_t *blkmgr)/*{{{*/
{
	arealst_rdlock(&blkmgr->blklst);

	area_t *area = (area_t *)&blkmgr->blklst;

	fprintf(stderr, "\033[1;36m blkmgr at $%.8x [%d areas]:\033[0m\n",
			(uint32_t)blkmgr, blkmgr->blklst.areacnt);

	bool error = FALSE;
	uint32_t areacnt = 0;

	while (TRUE) {
		area_valid(area);

		if (!area_is_guard(area)) {
			fprintf(stderr, "\033[1;31m  $%.8x - $%.8x: %8d : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area_begining(area), (uint32_t)area_end(area), area->size,
					(uint32_t)area->local.prev, (uint32_t)area->local.next);

			mb_print(mb_list_from_memarea(area));
		}
		else
			fprintf(stderr, "\033[1;33m  $%.8x %11s: %8s : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area, "", "guard", (uint32_t)area->local.prev, (uint32_t)area->local.next);

		if (area_is_guard(area->local.next))
			break;

		if (!area_is_guard(area) && (area >= area->local.next))
			error = TRUE;

		area = area->local.next;

		areacnt++;
	}

	assert(!error);

	assert(areacnt == blkmgr->blklst.areacnt);

	arealst_unlock(&blkmgr->blklst);
}/*}}}*/

