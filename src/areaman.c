#include "areaman.h"
#include <string.h>

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

	area->size  = PAGE_SIZE * SIZE_IN_PAGES(size);
	area->prev  = NULL;
	area->next  = NULL;

	ma_touch(area);

	DEBUG("Created memory area [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	return area;
}

/*
 * Add new memory area to memory area manager.
 */

void ma_add(memarea_t *newarea, memarea_t *mm)
{
	ma_valid(mm);
	ma_valid(newarea);
	assert(ma_is_guard(mm));

	DEBUG("will add area [$%.8x; %u; $%.2x] to memory manager\n", (uint32_t)newarea, newarea->size, newarea->flags);

	/* search the list for place where new area will be placed */
	memarea_t *area = mm;

	/* iterate till next area exists and has earlier address */
	while (TRUE) {
		assert(area != newarea);

		ma_valid(area);

		if (ma_is_guard(area->next) || (area->next > newarea))
			break;

		area = area->next;
	}

	/* newarea - memory area being inserted */
	newarea->next = area->next;
	newarea->prev = area;

	ma_touch(newarea);

	/* newarea->next - memory area before which new area is inserted */
	ma_valid(newarea->next);
		
	newarea->next->prev = newarea;

	ma_touch(newarea->next);

	/* area - memory area after which new area is inserted */
	area->next = newarea;

	ma_touch(area);

	DEBUG("inserted after area [$%.8x; %u; $%x]\n", (uint32_t)area, area->size, area->flags);
}

/*
 * Pull out the area from list of memory areas.
 */

static void ma_pullout(memarea_t *area)
{
	ma_valid(area);

	assert(!ma_is_guard(area));

	DEBUG("pulling out area [$%.8x, %u, $%.2x] from list\n", (uint32_t)area, area->size, area->flags);

	/* correct pointer in previous area */
	ma_valid(area->prev);

	area->prev->next = area->next;

	ma_touch(area->prev);

	/* correct pointer in next area */
	ma_valid(area->next);

	area->next->prev = area->prev;

	ma_touch(area->next);

	/* clear pointers in block being pulled out */
	area->next = NULL;
	area->prev = NULL;

	ma_touch(area);
}

/*
 * Remove memory area from list and unmap its memory.
 */

void ma_remove(memarea_t *area)
{
	ma_valid(area);
	assert(ma_is_mmap(area));

	ma_pullout(area);

	pm_mmap_free((void *)area, SIZE_IN_PAGES(area->size));

	DEBUG("removed area at $%.8x\n", (uint32_t)area);
}

/*
 * Coalesce memory area with adhering area.
 */

memarea_t *ma_coalesce(memarea_t *area, ma_coalesce_t *direction)
{
	ma_valid(area);
	assert(ma_is_mmap(area));

	DEBUG("will try to coalesce area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	/* coalesce with next area */
	ma_valid(area->next);

	if (!ma_is_guard(area->next) && ma_is_mmap(area->next) &&
		((uint32_t)area + area->size == (uint32_t)area->next))
	{
		memarea_t *next = area->next;

		ma_pullout(next);

		area->size += next->size;

		ma_touch(area);

		*direction = MA_COALESCE_RIGHT;

		DEBUG("coalesced with right neighbour [$%.8x; $%x; $%.2x]\n", (uint32_t)next, next->size, next->flags);
		DEBUG("coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);

		return area;
	}

	/* coalesce with previous area */
	ma_valid(area->prev);

	if (!ma_is_guard(area->prev) && ma_is_mmap(area->prev) &&
		((uint32_t)area->prev + area->prev->size == (uint32_t)area))
	{
		memarea_t *next = area;

		area = area->prev;

		ma_pullout(next);

		area->size += next->size;

		ma_touch(area);

		*direction = MA_COALESCE_LEFT;

		DEBUG("coalesced with left neighbour [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);
		DEBUG("coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);

		return area;
	}

	DEBUG("coalescing failed!\n");

	*direction = MA_COALESCE_FAILED;

	return area;
}

/*
 * Split memory area and unmap unused memory.
 */

void ma_split(memarea_t *area, uint32_t offset, uint32_t pages)
{
	ma_valid(area);
	assert(ma_is_mmap(area));

	assert((offset + pages) * PAGE_SIZE < area->size);

	/* Not a real splitting - just cutting off pages at the end of area */
	if ((offset + pages) * PAGE_SIZE == area->size) {
		pm_mmap_free((void *)((uint32_t)area + offset), pages);

		area->size -= pages * PAGE_SIZE;

		ma_touch(area);

		return;
	}

	/* Not a real splitting - just cutting off pages at the beginning of area */
	if (offset == 0) {
		memarea_t *oldarea = area;

		area = (memarea_t *)((uint32_t)area + pages * PAGE_SIZE);

		memcpy(area, oldarea, sizeof(memarea_t));

		area->size -= pages * PAGE_SIZE;

		ma_touch(area);

		if (area->next) {
			area->next->prev = area;

			ma_touch(area->next);
		}

		if (area->prev) {
			area->prev->next = area;

			ma_touch(area->prev);
		}

		pm_mmap_free((void *)area, pages);

		return;
	}

	/* Now unmapped pages are really inside area */
	memarea_t *newarea = (memarea_t *)((uint32_t)area + (offset + pages) * PAGE_SIZE);

	memcpy(newarea, area, sizeof(memarea_t));

	/* set up new area */
	newarea->size = area->size - (offset + pages) * PAGE_SIZE;
	newarea->prev = area;

	ma_touch(newarea);

	if (newarea->next) {
		newarea->next->prev = newarea;

		ma_touch(newarea->next);
	}

	/* correct data in splitted area */
	area->size = offset * PAGE_SIZE;
	area->next = newarea;

	ma_touch(area);
}

/*
 * Shrink sbrk memory area by 'pages' number of pages.
 */

bool ma_shrink(memarea_t *area, uint32_t pages)
{
	ma_valid(area);

	assert(ma_is_sbrk(area));
	assert(pages > 0);

	DEBUG("shrinking area $%.8x - %.8x by %u pages\n", (uint32_t)area, (uint32_t)area + area->size - 1, pages);

	if (pm_sbrk_free((void *)((uint32_t)area + area->size - (pages * PAGE_SIZE)), pages)) {
		area->size -= pages * PAGE_SIZE;

		ma_touch(area);

		return TRUE;
	}

	return FALSE;
}

/*
 * Expand sbrk memory area by 'pages' number of pages.
 */

bool ma_expand(memarea_t *area, uint32_t pages)
{
	ma_valid(area);

	assert(ma_is_sbrk(area));
	assert(pages > 0);

	DEBUG("expanding area $%.8x - $%.8x by %u pages\n", (uint32_t)area, (uint32_t)area + area->size - 1, pages);

	void *memory = pm_sbrk_alloc(pages);

	if (memory == NULL) {
		DEBUG("cannot get %u pages from\n", pages);
		return FALSE;
	}

	assert((uint32_t)area + area->size == (uint32_t)memory);

	area->size += pages * PAGE_SIZE;

	ma_touch(area);

	return TRUE;
}

/*
 * Initialize memory area manager.
 */

void ma_init_manager(memarea_t *mm)
{
	DEBUG("Initializing memory area manager.\n");

	mm->next  = mm;
	mm->prev  = mm;
	mm->flags = MA_FLAG_GUARD;
	mm->size  = 0;

	ma_touch(mm);
}
