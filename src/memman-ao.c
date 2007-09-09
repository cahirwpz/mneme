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
	ma_valid(mm);
	assert(ma_is_guard(mm));

	memarea_t  *area  = mm;
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

/*
 * Memory block deallocation procedure.
 */

void mm_free(memarea_t *mm, void *memory)
{
	memarea_t *area = mm;

	mb_free(mb_from_memarea(area), memory);

	int32_t pages = mb_can_shrink(mb_from_memarea(area)) - 3;

	if ((pages > 0) && ma_shrink(area, pages))
		mb_shrink(mb_from_memarea(area), pages);
}

/*
 * Print memory areas contents in given memory manager.
 */

void mm_print(memarea_t *mm)
{
	memarea_t *area = mm;

	while (area != NULL) {
		ma_valid(area);

		mb_print(mb_from_memarea(area));

		area = area->next;
	}
}
