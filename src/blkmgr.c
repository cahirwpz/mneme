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

void *blkmgr_alloc(blkmgr_t *self, uint32_t size, uint32_t alignment)/*{{{*/
{
	void *memory = NULL;

	if (alignment) {
		DEBUG("\033[37;1mRequested block of size %u aligned to %u bytes boundary.\033[0m\n", size, alignment);
	} else {
		DEBUG("\033[37;1mRequested block of size %u.\033[0m\n", size);
	}

	arealst_wrlock(&self->blklst);

	/* looking for an area with free space */
	area_t    *area = (area_t *)self->blklst.local.next;
	mb_list_t *list = NULL;

	while (!area_is_guard(area)) {
		assert(area_is_ready(area));

		DEBUG("searching for free block in [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags0);

		list   = mb_list_from_area(area);
		memory = (alignment > 0) ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);

		if (memory)
			break;

		area = area->local.next;
	}

	/* the area was not found - we must make some space */
	if (memory == NULL) {
		uint32_t area_size = size + sizeof(area_t) + sizeof(mb_list_t) + sizeof(mb_t);

		if (alignment > 0)
			area_size += alignment;

		DEBUG("Trying to merge adjacent pages to managed areas.\n");

		areamgr_prealloc_area(self->areamgr, SIZE_IN_PAGES(area_size));

		area = (area_t *)self->blklst.local.next;

		bool merged = FALSE;

		while (!area_is_guard(area)) {
			mb_list_t *list = mb_list_from_area(area);

			uint32_t oldsize = area->size;
			
			if (areamgr_expand_area(self->areamgr, &area, SIZE_IN_PAGES(area_size), LEFT)) {
				mb_list_t *to_merge = mb_list_from_area(area);

				mb_init(to_merge, area->size - oldsize - sizeof(area_t));

				list = mb_list_merge(to_merge, list, sizeof(area_t));

				merged = TRUE;
			} else if (areamgr_expand_area(self->areamgr, &area, SIZE_IN_PAGES(area_size), RIGHT)) {
				mb_list_t *to_merge = (mb_list_t *)((uint32_t)area_end(area) - (area->size - oldsize));

				mb_init(to_merge, area->size - oldsize - sizeof(area_t));

				list = mb_list_merge(list, to_merge, sizeof(area_t));

				merged = TRUE;
			}

			if (merged) {
				mb_list_t *to_merge = mb_list_from_area(area->local.next);

				if (area_end(area) == area_begining(area->local.next)) {
					assert(area->local.next == area->global.next);

					arealst_remove_area(&self->blklst, area->local.next, DONTLOCK);
					arealst_join_area(&self->areamgr->global, area, area->global.next, LOCK);

					list = mb_list_merge(list, to_merge, sizeof(area_t));
				}

				memory = alignment ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);
				break;
			}

			area = area->local.next;
		}

		if (memory == NULL) {
			DEBUG("No adjacent areas found - try to create new blocks' manager.\n");

			area_t *newarea = areamgr_alloc_area(self->areamgr, SIZE_IN_PAGES(area_size));

			if (newarea != NULL) {
				mb_list_t *list = mb_list_from_area(newarea);

				mb_init(list, newarea->size - sizeof(area_t));
				newarea->ready = TRUE;
				newarea->manager = AREA_MGR_BLKMGR;
				area_touch(newarea);

				arealst_insert_area_by_addr(&self->blklst, (void *)newarea, DONTLOCK);

				memory = alignment ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);
			}
		}
	}

	arealst_unlock(&self->blklst);

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
		result = mb_resize(mb_list_from_area(area), memory, new_size);

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
		mb_list_t *list = mb_list_from_area(area);

		mb_free_t *free = mb_free(list, memory);

		result = TRUE;

		/* is area completely empty (has exactly one block and it's free) */
		if ((blkmgr->blklst.areacnt > 1) && mb_is_first(list->next) && mb_is_last(list->next)) {
			arealst_remove_area(&blkmgr->blklst, (void *)area, DONTLOCK);
			areamgr_free_area(blkmgr->areamgr, area);
		} else {
			/* can area be shrinked at the end ? */

			shrink_right_pages = mb_list_can_shrink_at_end(list, sizeof(area_t));

			if (shrink_right_pages > 0) {
				mb_list_shrink_at_end(list, shrink_right_pages, sizeof(area_t));
				areamgr_shrink_area(blkmgr->areamgr, &area, SIZE_IN_PAGES(area->size) - shrink_right_pages, RIGHT);
			}

			/* can area be shrinked at the beginning ? */
			shrink_left_pages = mb_list_can_shrink_at_beginning(list, sizeof(area_t));

			if (shrink_left_pages > 0) {
				mb_list_shrink_at_beginning(&list, shrink_left_pages, sizeof(area_t));
				areamgr_shrink_area(blkmgr->areamgr, &area, SIZE_IN_PAGES(area->size) - shrink_left_pages, LEFT);
			}

			/* can area be splitted ? */
			cut_pages = mb_list_find_split(list, &free, &cut_addr, sizeof(area_t));

			if (cut_pages > 1) {
				area_t *leftover = NULL;

				mb_list_split(mb_list_from_area(area), free, cut_pages, sizeof(area_t));
				arealst_split_area(&blkmgr->areamgr->global, &area, &leftover, SIZE_IN_PAGES(cut_addr - area_begining(area)), LOCK);
				areamgr_shrink_area(blkmgr->areamgr, &leftover, SIZE_IN_PAGES(leftover->size) - cut_pages, LEFT);
				arealst_insert_area_by_addr(&blkmgr->blklst, leftover, DONTLOCK);
			}
		}
	}

	arealst_unlock(&blkmgr->blklst);

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

			mb_print(mb_list_from_area(area));
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

