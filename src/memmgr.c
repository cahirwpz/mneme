#include "memmgr.h"

/*
 * Initialize new memory area.
 */

struct memarea *ma_new(pm_type_t type, uint32_t size)
{
	memarea_t *area = NULL;

	switch (type)
	{
		case PM_SBRK:
	 		area = (memarea_t *)pm_sbrk_alloc(SIZE_IN_PAGES(size));
			area->flags = MA_FLAG_SBRK;
			break;

		case PM_MMAP:
	 		area = (memarea_t *)pm_mmap_alloc(SIZE_IN_PAGES(size));
			area->flags = MA_FLAG_MMAP;
			break;

		case PM_SHM:
	 		area = (memarea_t *)pm_shm_alloc(SIZE_IN_PAGES(size));
			area->flags = MA_FLAG_SHM;
			break;
	}

	area->free  = NULL;
	area->size  = PAGE_SIZE * SIZE_IN_PAGES(size);
	area->used  = area->size;
	area->prev  = NULL;
	area->next  = NULL;

	ma_touch(area);

	DEBUG("Created memory area [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	return area;
}

/*
 * Add new memory area to memory manager.
 */

void ma_insert(memarea_t *area, memmgr_t *mm)
{
	ma_valid(mm->areas);
	ma_valid(area);

	DEBUG("will add area [$%.8x; %u; $%x] to memory manager\n", (uint32_t)area, area->size, area->flags);

	/* check whether to insert before first area */
	if (mm->areas > area) {
		area->next = mm->areas;
		area->prev = NULL;

		ma_touch(area);
		
		mm->areas->prev = area;
		mm->areas	    = area;

		ma_touch(area->next);

		return;
	}

	/* search the list for place where new area will be placed */
	memarea_t *currarea = mm->areas;

	/* iterate till next area exists and has earlier address */
	while (TRUE) {
		assert(currarea != area);

		ma_valid(currarea);

		if ((currarea->next == NULL) || (currarea->next > area))
			break;

		currarea = currarea->next;
	}

	/* area - memory area being inserted */
	area->next	= currarea->next;
	area->prev	= currarea;

	ma_touch(area);

	/* area->next - memory area before which new area is inserted */
	if (area->next) {
		ma_valid(area->next);
		
		area->next->prev = area;

		ma_touch(area->next);
	}

	/* currarea - memory area after which new area is inserted */
	currarea->next = area;

	ma_touch(currarea);

	DEBUG("inserted after area [$%.8x; %u; $%x]\n", (uint32_t)currarea, currarea->size, currarea->flags);
}

/*
 * Coalesce memory area with adhering areas.
 */

memarea_t *ma_coalesce(memarea_t *area, memmgr_t *mm)
{
	return NULL;
}

/*
 * Print memory areas.
 */

void mm_print(memmgr_t *mm)
{
	memarea_t *area = mm->areas;

	while (area != NULL) {
		ma_valid(area);

		ma_print(area);

		area = area->next;
	}
}

/*
 * Initialize memory manager.
 */

void mm_init(memmgr_t *mm)
{
	DEBUG("Initializing memory manager.\n");

#ifdef PM_USE_SBRK
	pm_sbrk_init();
#endif

#ifdef PM_USE_MMAP
	pm_mmap_init();
#endif

#ifdef PM_USE_SHM
	pm_shm_init();
#endif

	mm->areas = ma_new(PM_SBRK, 4 * PAGE_SIZE);
}
