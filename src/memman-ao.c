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
	ma_add(ma_new(PM_SBRK, 4 * PAGE_SIZE), mm);
}

/*
 * Memory block allocation procedure.
 */

void *mm_alloc(memarea_t *mm, uint32_t size)
{
	memarea_t *area;

	ma_valid(mm);
	assert(ma_is_guard(mm));

	/* try to find area that have enough space to store allocated block */
	area = mm->next;

	while (!ma_is_guard(area)) {
		memblock_t *guard = mb_from_memarea(area);

		if (!ma_is_ready(area)) {
			mb_init(guard, area->size - ((uint32_t)guard - (uint32_t)area));

			area->flags |= MA_FLAG_READY;
			ma_touch(area);
		} 

		mb_valid(guard);

		void *memory = NULL;

		if ((memory = mb_alloc(guard, size, FALSE)))
			return memory;

		area = area->next;
	}
	
	/* not enough memory - try to get some from operating system */
	area = mm->next;

	while (!ma_is_guard(area)) {
		memblock_t *guard = mb_from_memarea(area);

		mb_valid(guard);

		void *memory = NULL;

		if (ma_is_sbrk(area) && (ma_expand(area, SIZE_IN_PAGES(size)))) {
			mb_expand(guard, SIZE_IN_PAGES(size));
			memory = mb_alloc(guard, size, TRUE);
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

	memarea_t *area = mm->next;

	while (TRUE) {
		memblock_t *guard = mb_from_memarea(area);

		/* does pointer belong to this area ? */
		if (((uint32_t)memory > (uint32_t)guard) && ((uint32_t)memory < (uint32_t)guard + guard->size)) {
			mb_free(guard, memory);

			if (ma_is_sbrk(area)) {
				int32_t pages = mb_can_shrink(guard) - 3;

				if ((pages > 0) && ma_shrink(area, pages))
					mb_shrink(guard, pages);
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
	ma_valid(mm);
	assert(ma_is_guard(mm));

	memarea_t *area = mm->next;

	while (!ma_is_guard(area)) {
		ma_valid(area);

		mb_print(mb_from_memarea(area));

		area = area->next;
	}
}
