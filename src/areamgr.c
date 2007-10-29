#include "areamgr.h"
#include <string.h>

/**
 * Get memory thru a system call and make it a new memory area.
 *
 * @param type
 * @param pages
 * @return
 */

area_t *area_new(pm_type_t type, uint32_t pages)
{
	void   *begining = NULL;
	area_t *area = NULL;

	switch (type) {
		case PM_SBRK:
	 		begining = pm_sbrk_alloc(pages);
			break;
		case PM_MMAP:
	 		begining = pm_mmap_alloc(pages);
			break;
		case PM_SHM:
			begining = pm_shm_alloc(pages);
			break;
	}

	if (begining == NULL)
		return NULL;

	area = area_footer(begining, pages);

	switch (type) {
		case PM_SBRK:
			area->flags = AREA_FLAG_SBRK;
			break;
		case PM_MMAP:
			area->flags = AREA_FLAG_MMAP;
			break;
		case PM_SHM:
			area->flags = AREA_FLAG_SHM;
			break;
	}

	area->size  = PAGE_SIZE * pages;

	area->pred	= NULL;
	area->succ	= NULL;
	area->prev  = NULL;
	area->next  = NULL;

	area_touch(area);

	DEBUG("Created memory area [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	return area;
}

/**
 * Remove memory area from list and unmap its memory. If unmapping failed
 * return area to manager. Area must be marked as used, and cannot be linked
 * into global list.
 *
 * @param area
 * @return
 */

bool area_delete(area_t *area)
{
	area_valid(area);
	assert(area_is_used(area));
	assert(area_is_mmap(area));

	if (pm_mmap_free(area_begining(area), SIZE_IN_PAGES(area->size))) {
		DEBUG("removed area at $%.8x\n", (uint32_t)area);

		return TRUE;
	}

	DEBUG("cannot remove area at $%.8x\n", (uint32_t)area);

	return FALSE;
}

/**
 * Join two adjacent areas.
 *
 * Both areas given as input arguments must be marked as used. Global list of
 * areas has to be locked.
 *
 * @param first
 * @param second
 * @return
 */

area_t *area_join(area_t *first, area_t *second)
{
	area_valid(first);
	area_valid(second);

	assert(area_is_used(first));
	assert(area_is_used(second));

	assert(((void *)first + sizeof(area_t) == area_begining(second)));

	/* Sum sizes */
	second->size += first->size;

	/* Remove first area from global list */
	area_valid(first->pred);

	first->pred->succ = first->succ;
	first->succ->pred = first->pred;

	area_touch(first->pred);
	area_touch(first->succ);

	/* Invalidate removed area */
	first->pred = NULL;
	first->succ = NULL;

	return NULL;
}

/**
 * Split memory area.
 *
 * Area given as input argument must be marked as used. Global list of areas
 * has to be locked.
 *
 * @param to_split	address to area with lower address
 * @param pages		size of area with lower address
 * @return			address to area with higher address
 */

area_t *area_split(area_t **to_split, uint32_t pages)
{
	area_t *area = *to_split;

	area_valid(area);
	assert(area_is_used(area));

	DEBUG("will split area [$%.8x; %u; $%.2x] at $%.8x\n",
		  (uint32_t)area, area->size, area->flags, (uint32_t)area_begining(area) + pages * PAGE_SIZE);

	assert(pages * PAGE_SIZE < area->size);

	/* Now split point is inside area */
	area_t *newarea = area_footer(area_begining(area), pages);

	memcpy(newarea, area, sizeof(area_t));

	/* set up new area */
	newarea->size = area->size - pages * PAGE_SIZE;
	newarea->pred = area;

	area_touch(newarea);

	if (newarea->succ) {
		newarea->succ->pred = newarea;

		area_touch(newarea->succ);
	}

	/* correct data in splitted area */
	area->size = pages * PAGE_SIZE;
	area->succ = newarea;

	area_touch(area);

	DEBUG("area splitted to [$%.8x; %u; $%.2x] and [$%.8x; %u; $%.2x]\n",
		  (uint32_t)area, area->size, area->flags, (uint32_t)newarea, newarea->size, newarea->flags);

	return area;
}


/**
 * Take area and use it begining as space for area manager.
 *
 * @param area
 * @return
 */

areamgr_t *areamgr_init(area_t *area)
{
	area_valid(area);

	/* Check if area has enough space to hold area manager's structure */
	assert(area->size - sizeof(area_t) >= sizeof(areamgr_t));

	areamgr_t *areamgr = area_begining(area);

	int i;

	for (i = 0; i < AREAMGR_LIST_COUNT; i++)
	{
		arealst_t *arealst = &areamgr->list[i];

		arealst->size    = (i + 1) * PAGE_SIZE;
		arealst->areacnt = 0;
		arealst->pred	 = NULL;
		arealst->succ	 = NULL;
		arealst->prev	 = (area_t *)arealst;
		arealst->next	 = (area_t *)arealst;
		arealst->flags	 = AREA_FLAG_LSTGUARD;

		area_touch((area_t *)arealst);
	}

	areamgr->pagecnt = 0;
	areamgr->guard	 = area;

	pthread_mutexattr_init(&areamgr->lock_attr);
	pthread_mutexattr_setpshared(&areamgr->lock_attr, 1);
	pthread_mutex_init(&areamgr->lock, &areamgr->lock_attr);

	area->flags |= AREA_FLAG_GUARD;

	area_touch(area);

	DEBUG("created area manager at $%.8x\n", (uint32_t)areamgr);

	return areamgr;
}

/**
 * Add new memory area to memory area manager.
 *
 * @param areamgr
 * @param newarea
 * @return
 */

void areamgr_add_area(areamgr_t *areamgr, area_t *newarea)
{
	area_valid(newarea);

	DEBUG("will add area [$%.8x; %u; $%.2x] to memory manager\n", (uint32_t)newarea, newarea->size, newarea->flags);

	/* FIRST STEP: Insert onto all areas' list. */

	/* search the list for place where new area will be placed */
	area_t *area = areamgr->guard;

	/* iterate till successor exists and has earlier address */
	while (TRUE) {
		assert(area != newarea);

		area_valid(area);

		if (area_is_guard(area->succ) || (area->succ > newarea))
			break;

		area = area->succ;
	}

	/* newarea - memory area being inserted */
	area_valid(newarea->succ);

	newarea->succ   = area->succ;
	newarea->pred   = area;
	newarea->flags |= AREA_FLAG_USED;
		
	newarea->succ->pred = newarea;
	newarea->pred->succ = newarea;

	area_touch(newarea);
	area_touch(newarea->pred);
	area_touch(newarea->succ);

	areamgr->pagecnt += SIZE_IN_PAGES(newarea->size);

	/* SECOND STEP: Area is treated as it was used - so make it free */

	areamgr_free_area(areamgr, newarea);

	DEBUG("inserted after area [$%.8x; %u; $%x]\n", (uint32_t)area, area->size, area->flags);
}

/**
 * Remove the area from the global-list. Area must be marked as used.
 *
 * @param areamgr
 * @return
 */

void areamgr_remove_area(areamgr_t *areamgr, area_t *area)
{
	area_valid(area);

	assert(!area_is_guard(area));
	assert(area_is_used(area));

	DEBUG("remove area [$%.8x, %u, $%.2x] from the global list\n", (uint32_t)area, area->size, area->flags);

	/* Remove area from global list */
	area_valid(area->pred);
	area_valid(area->succ);

	area->pred->succ = area->succ;
	area->succ->pred = area->pred;

	area_touch(area->pred);
	area_touch(area->succ);

	/* clear pointers in block being pulled out */
	area->succ = NULL;
	area->pred = NULL;

	areamgr->pagecnt -= SIZE_IN_PAGES(area->size);

	area_touch(area);
}

/**
 * Allocate memory area from area manager.
 *
 * If as a result no area was found then new area has to be created.
 *
 * @param areamgr
 * @param pages
 * @return
 */

area_t *areamgr_alloc_area(areamgr_t *areamgr, uint32_t pages, area_t *addr)
{
	bool by_addr = (pages == 0);

	if (by_addr)
		assert(addr != NULL);

	/* search the list for place where new area will be placed */
	int32_t n = by_addr ? 0 : (pages - 1);

	if (n >= AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT;

	area_t *area = NULL;

	while ((area == NULL) && (n < AREAMGR_LIST_COUNT)) {
		area = areamgr->list[n].next;

		/* check if there is an area of proper size on the list */
		while (TRUE) {
			area_valid(area);

			if (area_is_guard(area)) {
				/* check list for bigger sizes */
				n++;
				
				area = NULL;
				break;
			}

			if (by_addr && (area == addr))
				break;

			if (!by_addr && (area->size >= pages * PAGE_SIZE))
				break;

			area = area->next;
		}
	}

	if (area != NULL) {
		/* Remove area from free-list */
		area_valid(area->prev);
		area_valid(area->next);

		area->prev->next = area->next;
		area->next->prev = area->prev;

		area_touch(area->prev);
		area_touch(area->next);

		/* Decrement area counter in current free-list */
		areamgr->list[n].areacnt--;

		/* Mark area as used */
		area->flags |= AREA_FLAG_USED;
	}

	/* NOTE: If area is too big it should be splitted and left-over should be reinserted */

	return area;
}

/**
 * Free memory area to area manager.
 *
 * If suitable then before insertion area should be coalesced with adjacent
 * areas. After insertion a threshold can be exceeded triggering a few pages
 * being returned to operating system.
 *
 * @param areamgr
 * @param newarea
 * @return
 */

void areamgr_free_area(areamgr_t *areamgr, area_t *newarea)
{
	area_valid(newarea);

	/* qualify area for insertion into proper free-list */
	int n = SIZE_IN_PAGES(newarea->size) - 1;

	if (n >= AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT - 1;

	/* search the list for place where new area will be placed */
	area_t *area = (area_t *)&areamgr->list[n];

	/* iterate till successor exists and has earlier address */
	while (TRUE) {
		assert(area != newarea);

		area_valid(area);

		if (area_is_guard(area->next) || (area->next->size > newarea->size))
			break;

		area = area->next;
	}

	/* newarea - memory area being inserted */
	area_valid(area->next);

	newarea->next = area->next;
	newarea->prev = area;

	newarea->next->prev = newarea;
	newarea->prev->next = newarea;

	area_touch(newarea->next);
	area_touch(newarea->prev);
	area_touch(newarea);

	/* Mark area as free */
	newarea->flags &= ~AREA_FLAG_USED;

	/* Increment area counter in current free-list */
	areamgr->list[n].areacnt++;
}

/**
 * Coalesce memory area with adjacent areas.
 *
 * Both free-list and global-lock must be taken.
 *
 * @param aremgr
 * @param area
 * @param direction
 * @return
 */

area_t *areamgr_coalesce_area(areamgr_t *areamgr, area_t *area)
{
	area_valid(area);
	assert(!area_is_used(area));

	DEBUG("will try to coalesce area [$%.8x; $%x; $%.2x] with adjacent areas\n",
		  (uint32_t)area, area->size, area->flags);

	area_valid(area->succ);
	area_valid(area->pred);

	/* coalesce with succ area */
	while (!area_is_guard(area->succ) && !area_is_used(area->succ) &&
		   ((void *)area + sizeof(area_t) == area_begining(area->succ)))
	{
		DEBUG("coalescing with right neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->succ, area->succ->size, area->succ->flags);

		area = area_join(areamgr_alloc_area(areamgr, 0, area),
						 areamgr_alloc_area(areamgr, 0, area->succ));

		DEBUG("coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);
	}

	/* coalesce with previous area */
	while (!area_is_guard(area->pred) && !area_is_used(area->pred) &&
		((void *)area->pred + sizeof(area_t) == area_begining(area)))
	{
		DEBUG("coalescing with left neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->pred, area->pred->size, area->pred->flags);

		area = area_join(areamgr_alloc_area(areamgr, 0, area->pred),
						 areamgr_alloc_area(areamgr, 0, area));

		DEBUG("coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);
	}

	areamgr_free_area(areamgr, area);

	return area;
}

#if 0
/**
 * Shrink memory area by 'pages' number of pages.
 */

bool area_shrink_at_end(area_t *area, uint32_t pages)
{
	area_valid(area);
	assert(area_is_sbrk(area) || area_is_mmap(area));
	assert(pages > 0);

	DEBUG("will shrink area [$%.8x; %u; $%.2x] at the end by %u pages\n",
		  (uint32_t)area, area->size, area->flags, pages);

	void *address = (void *)((uint32_t)area + area->size - (pages * PAGE_SIZE));
	bool result	  = FALSE;

	if (area_is_sbrk(area))
		result = pm_sbrk_free(address, pages);

	if (area_is_mmap(area))
		result = pm_mmap_free(address, pages);

	if (!result) {
		DEBUG("cannot unmap memory\n");
		return FALSE;
	}

	area->size -= pages * PAGE_SIZE;

	area_touch(area);

	DEBUG("shrinked area [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	return TRUE;
}

/**
 *
 */

bool area_shrink_at_beginning(area_t **to_shrink, uint32_t pages)
{
	area_t *area = *to_shrink;

	area_valid(area);

	assert(area_is_mmap(area));
	assert(pages > 0);

	DEBUG("will shrink area [$%.8x; %u; $%.2x] at the beginning by %u pages\n",
		  (uint32_t)area, area->size, area->flags, pages);

	area_t *newarea = (area_t *)((uint32_t)area + pages * PAGE_SIZE);

	memcpy(newarea, area, sizeof(areamgr_t));

	if (!pm_mmap_free((void *)area, pages)) {
		DEBUG("cannot unmap memory\n");
		return FALSE;
	}

	newarea->size		-= pages * PAGE_SIZE;
	newarea->succ->pred  = newarea;
	newarea->pred->succ  = newarea;

	area_touch(newarea);
	area_touch(newarea->pred);
	area_touch(newarea->succ);

	*to_shrink = newarea;

	DEBUG("area shrinked to [$%.8x; %u; $%.2x]\n", (uint32_t)newarea, newarea->size, newarea->flags);

	return TRUE;
}

/*
 * Expand sbrk memory area by 'pages' number of pages.
 */

bool area_expand(area_t *area, uint32_t pages)
{
	area_valid(area);

	assert(area_is_sbrk(area));
	assert(pages > 0);

	DEBUG("expanding area $%.8x - $%.8x by %u pages\n", (uint32_t)area, (uint32_t)area + area->size - 1, pages);

	void *memory = pm_sbrk_alloc(pages);

	if (memory == NULL) {
		DEBUG("cannot get %u pages from\n", pages);
		return FALSE;
	}

	assert((uint32_t)area + area->size == (uint32_t)memory);

	area->size += pages * PAGE_SIZE;

	area_touch(area);

	return TRUE;
}
#endif
