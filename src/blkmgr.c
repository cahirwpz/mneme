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

void blkmgr_init(blkmgr_t *blkmgr, areamgr_t *areamgr)
{
	arealst_init(&blkmgr->blklst);

	blkmgr->areamgr = areamgr;
}

/**
 * Memory block allocation procedure.
 * @param mm
 * @param size
 * @param alignment
 * @return
 */

void *blkmgr_alloc(blkmgr_t *blkmgr, uint32_t size, uint32_t alignment)
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
	
	/* not enough memory - try to get some from operating system */
	DEBUG("Not enough memory - trying to get some from OS.\n");

	uint32_t area_size = size + sizeof(area_t) + sizeof(mb_list_t) + sizeof(mb_t);

	if (alignment)
		area_size += alignment;

	area_t *newarea = areamgr_alloc_area(blkmgr->areamgr, SIZE_IN_PAGES(area_size));

	/* prepare new list of blocks */
	mb_list_t *list = mb_list_from_memarea(newarea);

	mb_init(list, newarea->size - sizeof(area_t));

	newarea->flags |= AREA_FLAG_READY;
	area_touch(newarea);

	mb_valid(list);

	// area_t *arealst_join_area(arealst_t *global, area_t *first, area_t *second, locking_t locking)

	arealst_insert_area_by_addr(&blkmgr->blklst, (void *)newarea, LOCK);

	return alignment ? mb_alloc_aligned(list, size, alignment) : mb_alloc(list, size, FALSE);
}

/**
 * Resizing allocated block procedure.
 * @param
 * @param
 * @return
 */

bool blkmgr_realloc(blkmgr_t *blkmgr, void *memory, uint32_t new_size)
{
	DEBUG("\033[37;1mResizing block at $%.8x to %u bytes.\033[0m\n", (uint32_t)memory, new_size);

	area_t *area = blkmgr->blklst.local.next;

	while (TRUE) {
		mb_list_t *list = mb_list_from_memarea(area);

		/* does pointer belong to this area ? */
		if (((uint32_t)memory > (uint32_t)list) && ((uint32_t)memory < (uint32_t)list + list->size))
			return mb_resize(list, memory, new_size);

		area = area->local.next;

		/* if that happens, user has given wrong pointer */
		assert(!area_is_guard(area));
	}
}

/**
 * Memory block deallocation procedure.
 * @param mm
 * @param memory
 */

bool blkmgr_free(blkmgr_t *blkmgr, void *memory)
{
	DEBUG("\033[37;1mRequested to free block at $%.8x.\033[0m\n", (uint32_t)memory);

	bool result = FALSE;

	area_t *area = blkmgr->blklst.local.next;

	while (!area_is_guard(area)) {
		mb_list_t *list = mb_list_from_memarea(area);

		/* does pointer belong to this area ? */
		if (((uint32_t)memory > (uint32_t)list) && ((uint32_t)memory < (uint32_t)list + list->size)) {
			/* mb_free_t *free = */ mb_free(list, memory);

			result = TRUE;

			/* uint32_t pages; */

			/* is area completely empty (has exactly one block and it's free) */
			if ((blkmgr->blklst.areacnt > 1) && (list->next->flags & MB_FLAG_FIRST) &&
					(list->next->flags & MB_FLAG_LAST))
			{
				/* assert(ma_remove(area)); */
				break;
			}
#if 0
			/* can area be shrinked at the end ? */
			pages = mb_list_can_shrink_at_end(list);

			if (pages > 0) {
				mb_list_shrink_at_end(list, pages);
				assert(ma_shrink_at_end(area, pages));
			}

			/* can area be shrinked at the beginning ? */
			pages = mb_list_can_shrink_at_beginning(list, sizeof(area_t));

			if (pages > 0) {
				mb_list_shrink_at_beginning(&list, pages, sizeof(area_t));
				assert(ma_shrink_at_beginning(&area, pages));
			}
#endif

#if 0
			/* can area be splitted ? */
			void *cut = NULL;

			pages = mb_list_find_split(list, &free, &cut, sizeof(memarea_t));

			if (pages > 0) {
				mb_list_split(mb_list_from_memarea(area), free, pages, sizeof(memarea_t));

				area = ma_split(area, cut, pages);
			}
#endif
			break;
		}

		area = area->local.next;
	}

	return result;
}

/*
 * Print memory areas contents in given memory manager.
 */

void blkmgr_print(blkmgr_t *blkmgr)
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
}

