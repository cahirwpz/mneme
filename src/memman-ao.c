/*
 * Author:	Krystian Bacławski <krystian.baclawski@gmail.com>
 */

#include "memman-ao.h"
#include "blkman-ao.h"

/*
 * Memory manager initialization.
 */

void mm_init(memarea_t *mm)
{
#ifdef PM_USE_SBRK
	pm_sbrk_init();
#endif

#ifdef PM_USE_MMAP
	pm_mmap_init();
#endif

#ifdef PM_USE_SHM
	pm_shm_init();
#endif

	ma_init_manager(mm);
	ma_add(ma_new(PM_MMAP, 4 * PAGE_SIZE), mm);
}

/*
 * Memory block allocation procedure.
 */

void *mm_alloc(memarea_t *mm, uint32_t size)
{
	DEBUG("\033[37;1mRequested block of size %u.\033[0m\n", size);

	ma_valid(mm);
	assert(ma_is_guard(mm));

	/* try to find area that have enough space to store allocated block */
	DEBUG("browsing memory areas' list\n");

	memarea_t *area = mm->next;

	while (!ma_is_guard(area)) {
		mb_list_t *list = mb_list_from_memarea(area);

		DEBUG("searching for free block in [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

		if (!ma_is_ready(area)) {
			mb_init(list, area->size - ((uint32_t)list - (uint32_t)area));

			area->flags |= MA_FLAG_READY;
			ma_touch(area);
		} 

		mb_valid(list);

		void *memory = NULL;

		if ((memory = mb_alloc(list, size, FALSE)))
			return memory;

		area = area->next;
	}
	
	/* not enough memory - try to get some from operating system */
	DEBUG("Not enough memory - trying to obtain a few pages from system.\n");

	area = mm->next;

	while (!ma_is_guard(area)) {
		void *memory = NULL;

		if (ma_is_sbrk(area) && (ma_expand(area, SIZE_IN_PAGES(size)))) {
			mb_list_t *list = mb_list_from_memarea(area);

			mb_list_expand(list, SIZE_IN_PAGES(size));

			memory = mb_alloc(list, size, TRUE);
		}

		if (ma_is_mmap(area)) {
			memarea_t *newarea = ma_new(PM_MMAP, size + sizeof(memarea_t) + sizeof(mb_list_t) + sizeof(mb_t));
			
			/* prepare new list of blocks */
			mb_list_t *list = mb_list_from_memarea(newarea);

			mb_init(list, newarea->size - ((uint32_t)list - (uint32_t)newarea));

			newarea->flags |= MA_FLAG_READY;
			ma_touch(newarea);

			mb_valid(list);

			/* add it to area manager */
			ma_add(newarea, mm);

			ma_coalesce_t direction = MA_COALESCE_FAILED;

			while (TRUE) {
				memarea_t *area = newarea;
				memarea_t *next = newarea->next;

				mm_print(mm);

				newarea = ma_coalesce(newarea, &direction);

				if (direction == MA_COALESCE_FAILED)
					break;

				if (direction == MA_COALESCE_RIGHT)
					mb_list_merge(mb_list_from_memarea(area), mb_list_from_memarea(next), sizeof(memarea_t));

				if (direction == MA_COALESCE_LEFT)
					mb_list_merge(mb_list_from_memarea(newarea), mb_list_from_memarea(area), sizeof(memarea_t));
			}

			memory = mb_alloc(mb_list_from_memarea(newarea), size, FALSE);
		}

		if (memory)
			return memory;

		area = area->next;
	}

	return NULL;
}

/*
 * Memory block deallocation procedure.
 */

void mm_free(memarea_t *mm, void *memory)
{
	ma_valid(mm);
	assert(ma_is_guard(mm));

	DEBUG("\033[37;1mRequested to free block at $%.8x.\033[0m\n", (uint32_t)memory);

	memarea_t *area = mm->next;

	while (TRUE) {
		mb_list_t *list = mb_list_from_memarea(area);

		/* does pointer belong to this area ? */
		if (((uint32_t)memory > (uint32_t)list) && ((uint32_t)memory < (uint32_t)list + list->size)) {
			mb_free_t *free = mb_free(list, memory);

			if (ma_is_sbrk(area)) {
				int32_t pages = mb_list_can_shrink_at_end(list) - 3;

				if ((pages > 0) && ma_shrink_at_end(area, pages))
					mb_list_shrink_at_end(list, pages);
			}

			if (ma_is_mmap(area)) {
				uint32_t pages;

				/* is area completely empty (has exactly one block and it's free) */
				if ((area->next != area->prev) && (list->next->flags & MB_FLAG_FIRST) &&
					(list->next->flags & MB_FLAG_LAST))
				{
					assert(ma_remove(area));
					break;
				}

				/* can area be shrinked at the end ? */
				pages = mb_list_can_shrink_at_end(list);

				if (pages > 0) {
					mb_list_shrink_at_end(list, pages);
					assert(ma_shrink_at_end(area, pages));
				}

				/* can area be shrinked at the beginning ? */
				pages = mb_list_can_shrink_at_beginning(list, sizeof(memarea_t));

				if (pages > 0) {
					mb_list_shrink_at_beginning(&list, pages, sizeof(memarea_t));
					assert(ma_shrink_at_beginning(&area, pages));
				}

#if 1
				/* can area be splitted ? */
				void *cut = NULL;

				pages = mb_list_find_split(list, &free, &cut, sizeof(memarea_t));

				if (pages > 0) {
					mb_list_split(mb_list_from_memarea(area), free, pages, sizeof(memarea_t));

					area = ma_split(area, cut, pages);
				}
#endif
			}

			break;
		}

		area = area->next;

		/* if that happens, user has given wrong pointer */
		assert(!ma_is_guard(area));
	}
}

/*
 * Print memory areas contents in given memory manager.
 */

void mm_print(memarea_t *mm)
{
#if VERBOSE > 0
	ma_valid(mm);
	assert(ma_is_guard(mm));

	memarea_t *area = mm->next;

	while (!ma_is_guard(area)) {
		ma_valid(area);

		mb_print(mb_list_from_memarea(area));

		area = area->next;
	}
#endif
}
