#include "areamgr.h"
#include <string.h>

/**
 * A few words of comment.
 *
 * Flag AREA_FLAG_USED determines on which list an area is. When an area is not
 * marked as AREA_FLAG_USED then it is:
 *  - on free list
 *  - or is being inserted to free list.
 * When an area is marked as AREA_FLAG_USED then it is on free list.
 */

/**
 * Gets memory thru a system call and make it a new memory area.
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

	area->size			= PAGE_SIZE * pages;
	area->global.prev	= NULL;
	area->global.next	= NULL;
	area->local.prev	= NULL;
	area->local.next	= NULL;

	area_touch(area);

	DEBUG("Created memory area at $%.8x [$%.8x; %u; $%.2x]\n", (uint32_t)area,
			(uint32_t)area_begining(area), area->size, area->flags);

	return area;
}

/**
 * Removes the area completely - unmaps its memory.
 *
 * If unmapping failed area should be returned to manager. Area must be marked
 * as used, and obviously cannot be linked into global list.
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
		DEBUG("Removed area at $%.8x\n", (uint32_t)area);

		return TRUE;
	}

	DEBUG("Cannot remove area at $%.8x\n", (uint32_t)area);

	return FALSE;
}

/**
 * Initializes list of areas.
 *
 * @param arealst
 */

void arealst_init(arealst_t *arealst)
{
	arealst->size    		= 0;
	arealst->global.prev	= NULL;
	arealst->global.next	= NULL;
	arealst->local.prev		= (area_t *)arealst;
	arealst->local.next		= (area_t *)arealst;
	arealst->flags			= AREA_FLAG_GUARD;

	area_touch((area_t *)arealst);

	/* Initialize area counter */
	arealst->areacnt = 0;

	/* Initialize locking mechanizm */
	pthread_rwlockattr_init(&arealst->lock_attr);
	pthread_rwlockattr_setpshared(&arealst->lock_attr, 1);
	pthread_rwlock_init(&arealst->lock, &arealst->lock_attr);
}

/**
 * Adds area to global list.
 *
 * @param arealst this list it used as a global list.
 * @param newarea inserted area.
 * @param locking
 */

void arealst_global_add_area(arealst_t *arealst, area_t *newarea, locking_t locking)
{
	DEBUG("Will add area at $%.8x to global list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_valid(newarea);
	assert(area_is_global_guard((area_t *)arealst));

	/* search the list for place where new area will be placed */
	area_t *area = arealst->global.next;

	/* iterate till successor exists and has earlier address */
	while (TRUE) {
		assert(area != newarea);

		area_valid(area);

		if (area_is_global_guard(area->global.next) || (area > newarea))
			break;

		area = area->global.next;
	}

	area = area->global.prev;

	newarea->global.next  = area->global.next;
	newarea->global.prev  = area;
	newarea->flags 		 |= AREA_FLAG_USED;
		
	newarea->global.next->global.prev = newarea;
	newarea->global.prev->global.next = newarea;

	area_touch(newarea->global.prev);
	area_touch(newarea->global.next);
	area_touch(newarea);

	/* Increment area counter */
	arealst->areacnt++;

	area_touch((area_t *)arealst);

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Removes the area from global list.
 *
 * @param arealst
 * @param area
 */

void arealst_global_remove_area(arealst_t *arealst, area_t *area, locking_t locking)
{
	DEBUG("Will remove area at $%.8x from global list at $%.8x %s locking\n",
			(uint32_t)area, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	assert(area_is_global_guard((area_t *)arealst));

	/* Remove area from global list */
	area_valid(area->global.prev);
	area_valid(area->global.next);

	area->global.prev->global.next = area->global.next;
	area->global.next->global.prev = area->global.prev;

	area_touch(area->global.prev);
	area_touch(area->global.next);

	/* clear pointers in block being pulled out */
	area->global.next = NULL;
	area->global.prev = NULL;

	/* Decrement area counter */
	arealst->areacnt--;

	area_touch((area_t *)arealst);

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Finds on given list an area that contains given address.
 *
 * @param arealst
 * @param addr
 * @return
 */

area_t *arealst_find_area_by_addr(arealst_t *arealst, void *addr, locking_t locking)
{
	if (locking)
		arealst_rdlock(arealst);

	area_t *area = arealst->local.next;

	/* check if there is an area of proper size on the list */
	while (TRUE) {
		area_valid(area);

		if (area_is_guard(area))
			return NULL;

		if ((addr >= area_begining(area)) && (addr < area_end(area)))
			break;

		area = area->local.next;
	}

	if (locking)
		arealst_unlock(arealst);

	return area;
}

/**
 * Finds on given list an area of size greater or equal to given.
 *
 * @param arealst
 * @param size
 * @return
 */

area_t *arealst_find_area_by_size(arealst_t *arealst, uint32_t size, locking_t locking)
{
	if (locking)
		arealst_rdlock(arealst);

	area_t *area = arealst->local.next;

	/* check if there is an area of proper size on the list */
	while (TRUE) {
		area_valid(area);

		if (area_is_guard(area))
			return NULL;

		if (area->size >= size)
			break;

		area = area->local.next;
	}

	if (locking)
		arealst_unlock(arealst);

	return area;
}

/**
 * Inserts area after given item of supplied list.
 *
 * Procedure does not check if <i>after</i> is member of list.
 *
 * @param arealst
 * @param after
 * @return newarea
 */

void arealst_insert_area(arealst_t *arealst, area_t *after, area_t *newarea, locking_t locking)
{
	if (locking)
		arealst_wrlock(arealst);

	area_valid(after);

	newarea->local.next	= after->local.next;
	newarea->local.prev	= after;

	newarea->local.next->local.prev = newarea;
	newarea->local.prev->local.next = newarea;

	area_touch(newarea->local.next);
	area_touch(newarea->local.prev);
	area_touch(newarea);

	arealst->areacnt++;

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Finds area on list that has begining at given address. Insert <i>new area</i>
 * after it.
 *
 * @param arealst
 * @param newarea
 */

void arealst_insert_area_by_addr(arealst_t *arealst, area_t *newarea, locking_t locking)
{
	DEBUG("Will insert area at $%.8x to list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_t *area = arealst_find_area_by_addr(arealst, (void *)area_begining(newarea), DONTLOCK);

	return arealst_insert_area(arealst, area ? area->local.prev : (area_t *)arealst, newarea, DONTLOCK);

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Finds area on list that has size greater or equal to the given size Insert
 * <i>new area</i> after it.
 *
 * @param arealst
 * @param newarea
 * @return
 */

void arealst_insert_area_by_size(arealst_t *arealst, area_t *newarea, locking_t locking)
{
	DEBUG("Will insert area at $%.8x to list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_t *area = arealst_find_area_by_size(arealst, newarea->size, DONTLOCK);

	return arealst_insert_area(arealst, area ? area->local.prev : (area_t *)arealst, newarea, DONTLOCK);

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Removes the area from given list.
 * 
 * Procedure does not check if <i>area</i> is member of list.
 *
 * @param arealst
 * @param area
 * @return
 */

void arealst_remove_area(arealst_t *arealst, area_t *area, locking_t locking)
{
	DEBUG("Will remove area at $%.8x from list at $%.8x %s locking\n",
			(uint32_t)area, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_valid(area);
	assert(area_is_guard((area_t *)arealst));

	/* Remove area from the list */
	area_valid(area->local.prev);
	area_valid(area->local.next);

	area->local.prev->local.next = area->local.next;
	area->local.next->local.prev = area->local.prev;

	area_touch(area->local.prev);
	area_touch(area->local.next);
	area_touch(area);

	/* Decrement area counter of the list */
	arealst->areacnt--;

	if (locking)
		arealst_unlock(arealst);
}

/**
 * Joins two adjacent areas.
 *
 * Both areas given as input must be marked as used.
 *
 * @param global
 * @param first
 * @param second
 * @param locking
 * @return
 */

area_t *arealst_join_area(arealst_t *global, area_t *first, area_t *second, locking_t locking)
{
	if (locking)
		arealst_wrlock(global);

	area_valid(first);
	area_valid(second);

	assert(area_is_used(first));
	assert(area_is_used(second));

	assert(((void *)first + sizeof(area_t) == area_begining(second)));

	/* Sum sizes */
	second->size += first->size;

	/* Remove first area from global list */
	area_valid(first->global.prev);

	first->global.prev->global.next = first->global.next;
	first->global.next->global.prev = first->global.prev;

	area_touch(first->global.prev);
	area_touch(first->global.next);

	/* Invalidate removed area */
	first->global.prev = NULL;
	first->global.next = NULL;

	global->areacnt--;

	if (locking)
		arealst_unlock(global);

	return second;
}

/**
 * Splits memory area.
 *
 * Area given as input argument must be marked as used. Global list of areas
 * has to be locked.
 *
 * @param global
 * @param splitted	address to area with lower address
 * @param remainder	address to area with higher address
 * @param pages		size of area with lower address
 * @param locking
 */

void arealst_split_area(arealst_t *global, area_t **splitted, area_t **remainder, uint32_t pages, locking_t locking)
{
	if (locking)
		arealst_wrlock(global);

	area_t *area = *splitted;

	area_valid(area);
	assert(area_is_used(area));

	DEBUG("Will split area [$%.8x; %u; $%.2x] at $%.8x with cut point at $%.8x\n",
		  (uint32_t)area, area->size, area->flags, (uint32_t)area_begining(area),
		  (uint32_t)area_begining(area) + pages * PAGE_SIZE);

	assert(pages * PAGE_SIZE < area->size);

	/* Now split point is inside area */
	area_t *newarea = area_footer(area_begining(area), pages);

	memcpy(newarea, area, sizeof(area_t));

	newarea->local.next = NULL;
	newarea->local.prev = NULL;

	/* set up new area */
	newarea->size = pages * PAGE_SIZE;
	newarea->global.next = area;

	area_touch(newarea);

	newarea->global.prev->global.next = newarea;
	area_touch(newarea->global.prev);

	/* correct data in splitted area */
	area->size = area->size - pages * PAGE_SIZE;
	area->global.prev = newarea;

	area_touch(area);

	global->areacnt++;

	DEBUG("Area splitted to [$%.8x; %u; $%.2x] at $%.8x and [$%.8x; %u; $%.2x] at $%.8x\n",
		  (uint32_t)newarea, newarea->size, newarea->flags, (uint32_t)area_begining(newarea),
		  (uint32_t)area, area->size, area->flags, (uint32_t)area_begining(area));

	*splitted  = newarea;
	*remainder = area;

	assert(*splitted < *remainder);

	if (locking)
		arealst_unlock(global);
}

/**
 * Takes an area and use its begining as space for area manager.
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

	/* Initialize free areas' lists */
	int i;

	for (i = 0; i < AREAMGR_LIST_COUNT; i++)
		arealst_init(&areamgr->list[i]);

	/* Initialize global list */
	arealst_init(&areamgr->global);

	areamgr->global.global.next = (area_t *)&areamgr->global;
	areamgr->global.global.prev = (area_t *)&areamgr->global;
	areamgr->global.flags |= AREA_FLAG_GLOBAL_GUARD;
	areamgr->global.areacnt = 1;
	area_touch((area_t *)&areamgr->global);

	areamgr->pagecnt = SIZE_IN_PAGES(area->size);

	DEBUG("Created area manager at $%.8x\n", (uint32_t)areamgr);

	return areamgr;
}

/**
 * Adds new memory area to memory area manager.
 *
 * @param areamgr
 * @param newarea
 * @return
 */

void areamgr_add_area(areamgr_t *areamgr, area_t *newarea)
{
	area_valid(newarea);

	DEBUG("Will add area [$%.8x; %u; $%.2x] to memory manager\n", (uint32_t)newarea, newarea->size, newarea->flags);

	/* FIRST STEP: Insert onto all areas' list. */
	{
		arealst_wrlock(&areamgr->global);

		arealst_global_add_area(&areamgr->global, newarea, DONTLOCK);
		areamgr->pagecnt += SIZE_IN_PAGES(newarea->size);

		arealst_unlock(&areamgr->global);
	}

	/* SECOND STEP: Area is treated as it was used - so make it free */
	areamgr_free_area(areamgr, newarea);
}

/**
 * Remove the area from the global list.
 *
 * The area must be marked as used. After that action, the area can be deleted.
 *
 * @param areamgr
 * @param area
 * @return
 */

void areamgr_remove_area(areamgr_t *areamgr, area_t *area)
{
	area_valid(area);

	assert(!area_is_guard(area));
	assert(area_is_used(area));

	DEBUG("Remove area [$%.8x, %u, $%.2x] from the global list\n", (uint32_t)area, area->size, area->flags);

	/* Remove it! */
	{
		arealst_wrlock(&areamgr->global);

		arealst_global_remove_area(&areamgr->global, area, DONTLOCK);
		areamgr->pagecnt -= SIZE_IN_PAGES(area->size);

		arealst_unlock(&areamgr->global);
	}
}

/**
 * Allocates memory area from area manager.
 *
 * If as a result no area was found then a new area has to be created by
 * the caller. Both locks must be taken.
 *
 * @param areamgr
 * @param pages
 * @return
 */

area_t *areamgr_alloc_area(areamgr_t *areamgr, uint32_t pages, area_t *addr)
{
	bool by_addr = (pages == 0);

	DEBUG("Will try to find area of size %u or more pages\n", pages);

	/* address must be non-null if seeking area with given address */
	if (by_addr)
		assert(addr != NULL);

	/* begin searching from areas' list of proper size*/
	int32_t n = (by_addr) ? (0) : (pages - 1);

	/* last list stores areas of size bigger than AREAMGR_LIST_COUNT pages */
	if (n >= AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT;

	area_t *area = NULL;

	/* browse through lists till proper area is not found */
	while (n < AREAMGR_LIST_COUNT) {
		arealst_wrlock(&areamgr->list[n]);

		if (by_addr)
			area = arealst_find_area_by_addr(&areamgr->list[n], addr, DONTLOCK);
		else
			area = arealst_find_area_by_size(&areamgr->list[n], pages * PAGE_SIZE, DONTLOCK);

		if (area)
			break;

		arealst_unlock(&areamgr->list[n]);

		n++;
	}

	/* If area was found then reserve it */
	if (area != NULL) {
		DEBUG("Area found [$%.8x, %u, $%.2x] at $%.8x\n",
				(uint32_t)area, area->size, area->flags, (uint32_t)area_begining(area));

		arealst_remove_area(&areamgr->list[n], area, DONTLOCK);

		/* Mark area as used */
		area->flags |= AREA_FLAG_USED;

		area_touch(area);

		arealst_unlock(&areamgr->list[n]);

		/* If area is too big it should be splitted and left-over should be
		 * reinserted. */
		if (!by_addr && (area->size > pages * PAGE_SIZE)) {
			area_t *newarea = NULL;

			arealst_split_area(&areamgr->global, &area, &newarea, pages, LOCK);

			areamgr_free_area(areamgr, newarea);
		}
	} else {
		DEBUG("Area not found - will create one\n");

		/* If area was not found then create one */
		if ((area = area_new(PM_MMAP, pages))) {

			arealst_wrlock(&areamgr->global);

			arealst_global_add_area(&areamgr->global, area, DONTLOCK);

			areamgr->pagecnt += SIZE_IN_PAGES(area->size);

			arealst_unlock(&areamgr->global);
		}
	}

	return area;
}

/**
 * Frees memory area to area manager.
 *
 * If suitable then before insertion area should be coalesced with adjacent
 * areas. After insertion a threshold can be exceeded triggering a few pages
 * being returned to the operating system.
 *
 * Area <i>newarea</i> must be marked as used!
 *
 * @param areamgr
 * @param newarea
 * @return
 */

void areamgr_free_area(areamgr_t *areamgr, area_t *newarea)
{
	DEBUG("Will try to free area [$%.8x, %u, $%.2x] at $%.8x\n",
			(uint32_t)newarea, newarea->size, newarea->flags, (uint32_t)area_begining(newarea));

	/* mark area as free */
	{
		arealst_wrlock(&areamgr->global);

		area_valid(newarea);
		assert(area_is_used(newarea));
		newarea->flags &= ~AREA_FLAG_USED;
		area_touch(newarea);

		arealst_unlock(&areamgr->global);
	}

	/* qualify area for insertion into proper free-list */
	int n = SIZE_IN_PAGES(newarea->size) - 1;

	if (n >= AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT - 1;

	/* insert area on proper free list */
	arealst_insert_area_by_size(&areamgr->list[n], newarea, LOCK);
}

/**
 * Coalesces memory area with adjacent areas.
 *
 * @param aremgr
 * @param area
 * @param direction
 * @return
 */

#if 0
area_t *areamgr_coalesce_area(areamgr_t *areamgr, area_t *area)
{
	area_valid(area);
	assert(area_is_used(area));

	DEBUG("Will try to coalesce area [$%.8x; $%x; $%.2x] with adjacent areas\n",
		  (uint32_t)area, area->size, area->flags);

	area_valid(area->global.next);
	area_valid(area->global.prev);

	/* coalesce with global.next area */
	while (!area_is_guard(area->global.next) && !area_is_used(area->global.next) &&
		   ((void *)area + sizeof(area_t) == area_begining(area->global.next)))
	{
		DEBUG("Coalescing with right neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->global.next, area->global.next->size, area->global.next->flags);

		area = arealst_join_area(&areamgr->global,
								 areamgr_alloc_area(areamgr, 0, area),
						 		 areamgr_alloc_area(areamgr, 0, area->global.next),
								 DONTLOCK);

		DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);
	}

	/* coalesce with previous area */
	while (!area_is_guard(area->global.prev) && !area_is_used(area->global.prev) &&
		((void *)area->global.prev + sizeof(area_t) == area_begining(area)))
	{
		DEBUG("Coalescing with left neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->global.prev, area->global.prev->size, area->global.prev->flags);

		area = arealst_join_area(&areamgr->global,
								 areamgr_alloc_area(areamgr, 0, area->global.prev),
						 		 areamgr_alloc_area(areamgr, 0, area),
								 DONTLOCK);

		DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags);
	}

	areamgr_free_area(areamgr, area);

	return area;
}
#endif

/**
 *
 * @param areamgr
 * @param area
 * @param to_left
 * @param pages
 * @return
 */

#if 0
uint32_t areamgr_expand_area(areamgr_t *areamgr, area_t *area, bool to_left, uint32_t pages)
{
	area_valid(area);
	assert(!area_is_used(area));

	DEBUG("Will shrink area [$%.8x; %u; $%.2x] at the end by %u pages\n",
		  (uint32_t)area, area->size, area->flags, pages);

	void *address = (void *)((uint32_t)area + area->size - (pages * PAGE_SIZE));
	bool result	  = FALSE;

	if (area_is_sbrk(area))
		result = pm_sbrk_free(address, pages);

	if (area_is_mmap(area))
		result = pm_mmap_free(address, pages);

	if (!result) {
		DEBUG("Cannot unmap memory\n");
		return FALSE;
	}

	area->size -= pages * PAGE_SIZE;

	area_touch(area);

	DEBUG("Shrinked area [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);

	return TRUE;
}
#endif

/**
 *
 */

#if 0
bool area_shrink_at_beginning(area_t **to_shrink, uint32_t pages)
{
	area_t *area = *to_shrink;

	area_valid(area);

	assert(area_is_mmap(area));
	assert(pages > 0);

	DEBUG("Will shrink area [$%.8x; %u; $%.2x] at the beginning by %u pages\n",
		  (uint32_t)area, area->size, area->flags, pages);

	area_t *newarea = (area_t *)((uint32_t)area + pages * PAGE_SIZE);

	memcpy(newarea, area, sizeof(areamgr_t));

	if (!pm_mmap_free((void *)area, pages)) {
		DEBUG("Cannot unmap memory\n");
		return FALSE;
	}

	newarea->size		-= pages * PAGE_SIZE;
	newarea->global.next->global.prev  = newarea;
	newarea->global.prev->global.next  = newarea;

	area_touch(newarea);
	area_touch(newarea->global.prev);
	area_touch(newarea->global.next);

	*to_shrink = newarea;

	DEBUG("Area shrinked to [$%.8x; %u; $%.2x]\n", (uint32_t)newarea, newarea->size, newarea->flags);

	return TRUE;
}
#endif

/*
 * Expand sbrk memory area by 'pages' number of pages.
 */

#if 0
bool area_expand(area_t *area, uint32_t pages)
{
	area_valid(area);

	assert(area_is_sbrk(area));
	assert(pages > 0);

	DEBUG("Expanding area $%.8x - $%.8x by %u pages\n", (uint32_t)area, (uint32_t)area + area->size - 1, pages);

	void *memory = pm_sbrk_alloc(pages);

	if (memory == NULL) {
		DEBUG("Cannot get %u pages from\n", pages);
		return FALSE;
	}

	assert((uint32_t)area + area->size == (uint32_t)memory);

	area->size += pages * PAGE_SIZE;

	area_touch(area);

	return TRUE;
}
#endif
