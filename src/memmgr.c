#include "memmgr.h"

/*
 * Initialize new memory area.
 */

struct memarea *ma_new(pm_type_t type, uint32_t size)
{
	memarea_t *area;

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
 * Print memory areas.
 */

void mm_print(memarea_t *area)
{
	while (area != NULL) {
		ma_valid(area);

		ma_print(area);

		area = area->next;
	}
}

/*
 * Initialize memory manager.
 */

memarea_t *mm_init(void)
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

	return ma_new(PM_SBRK, 4 * PAGE_SIZE);
}
