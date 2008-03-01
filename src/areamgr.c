/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Manager for areas (sets of OS pages)
 */

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

area_t *area_new(pm_type_t type, uint32_t pages)/*{{{*/
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

	memset(area, 0, sizeof(area_t));

	/* Here is a little bug (in gcc?) - if this line is moved after switch block,
	 * checksum is calculated in a strange way */
	area->size = PAGE_SIZE * pages;
	area->used = TRUE;
	area->cpu  = 0;

	switch (type) {
		case PM_SBRK:
			area->type = AREA_TYPE_SBRK;
			break;
		case PM_MMAP:
			area->type = AREA_TYPE_MMAP;
			break;
		case PM_SHM:
			area->type = AREA_TYPE_SHM;
			break;
	}

	DEBUG("Created memory area at $%.8x [$%.8x; %u; $%.2x]\n", (uint32_t)area,
			(uint32_t)area_begining(area), area->size, area->flags0);

	area_touch(area);

	return area;
}/*}}}*/

/**
 * Removes the area completely - unmaps its memory.
 *
 * If unmapping failed area should be returned to manager. Area must be marked
 * as used, and obviously cannot be linked into global list.
 *
 * @param area
 * @return
 */

bool area_delete(area_t *area)/*{{{*/
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
}/*}}}*/

/**
 * Initializes list of areas.
 *
 * @param arealst
 */

void arealst_init(arealst_t *arealst)/*{{{*/
{
	memset(arealst, 0, sizeof(arealst_t));

	arealst->local.prev	= (area_t *)arealst;
	arealst->local.next	= (area_t *)arealst;
	arealst->guard		= TRUE;

	area_touch((area_t *)arealst);

	/* Initialize locking mechanizm */
	pthread_rwlockattr_init(&arealst->lock_attr);
	pthread_rwlockattr_setpshared(&arealst->lock_attr, 1);
	pthread_rwlock_init(&arealst->lock, &arealst->lock_attr);
}/*}}}*/

/**
 * Adds area to global list.
 *
 * @param arealst this list it used as a global list.
 * @param newarea inserted area.
 * @param locking
 */

void arealst_global_add_area(arealst_t *arealst, area_t *newarea, locking_t locking)/*{{{*/
{
	DEBUG("Will add area at $%.8x to global list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_valid(newarea);
	assert(area_is_global_guard((area_t *)arealst));

	/* search the list for place where new area will be placed */
	area_t *after = (area_t *)arealst;

	/* iterate till successor exists and has earlier address */
	while (TRUE) {
		area_valid(after);

		if (area_is_global_guard(after->global.next))
			break;
		
		if (area_begining(newarea) < area_begining(after->global.next))
			break;

		after = after->global.next;
	}

	DEBUG("Will insert after $%.8x at $%.8x\n", (uint32_t)after, (uint32_t)area_begining(after));

	newarea->global.next = after->global.next;
	newarea->global.prev = after;
	newarea->used		 = TRUE;
		
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
}/*}}}*/

/**
 * Removes the area from global list.
 *
 * @param arealst
 * @param area
 */

void arealst_global_remove_area(arealst_t *arealst, area_t *area, locking_t locking)/*{{{*/
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
}/*}}}*/

/**
 * Checks if given area belongs to the list.
 *
 * @param arealst
 * @param addr
 * @return
 */

bool arealst_has_area(arealst_t *arealst, area_t *addr, locking_t locking)/*{{{*/
{
	if (locking)
		arealst_rdlock(arealst);

	area_t *area = arealst->local.next;

	bool result = FALSE;

	/* check if there is an area of proper address on the list */
	while (TRUE) {
		area_valid(area);

		if (area_is_guard(area))
			break;

		if (addr == area) {
			result = TRUE;
			break;
		}

		area = area->local.next;
	}

	if (locking)
		arealst_unlock(arealst);

	return result;
}/*}}}*/

/**
 * Finds on given list an area that contains given address.
 *
 * @param arealst
 * @param addr
 * @return
 */

area_t *arealst_find_area_by_addr(arealst_t *arealst, void *addr, locking_t locking)/*{{{*/
{
	if (locking)
		arealst_rdlock(arealst);

	area_t *area = arealst->local.next;

	/* check if there is an area of proper address on the list */
	while (TRUE) {
		area_valid(area);

		if (area_is_guard(area)) {
			area = NULL;
			break;
		}

		if ((addr >= area_begining(area)) && (addr < area_end(area)))
			break;

		area = area->local.next;
	}

	if (locking)
		arealst_unlock(arealst);

	return area;
}/*}}}*/

/**
 * Finds on given list an area of size greater or equal to given.
 *
 * @param arealst
 * @param size
 * @return
 */

area_t *arealst_find_area_by_size(arealst_t *arealst, uint32_t size, locking_t locking)/*{{{*/
{
	if (locking)
		arealst_rdlock(arealst);

	area_t *area = arealst->local.next;

	/* check if there is an area of proper size on the list */
	while (TRUE) {
		area_valid(area);

		if (area_is_guard(area)) {
			area = NULL;
			break;
		}

		if (area->size >= size)
			break;

		area = area->local.next;
	}

	if (locking)
		arealst_unlock(arealst);

	return area;
}/*}}}*/

/**
 * Inserts area after given item of supplied list.
 *
 * Procedure does not check if <i>after</i> is member of list.
 *
 * @param arealst
 * @param after
 * @return newarea
 */

void arealst_insert_area(arealst_t *arealst, area_t *after, area_t *newarea, locking_t locking)/*{{{*/
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
}/*}}}*/

/**
 * Finds area on list that has begining at given address. Insert <i>new area</i>
 * after it.
 *
 * @param arealst
 * @param newarea
 */

void arealst_insert_area_by_addr(arealst_t *arealst, area_t *newarea, locking_t locking)/*{{{*/
{
	DEBUG("Will insert area at $%.8x [$%.8x; %u; $%.2x] to list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)area_begining(newarea), newarea->size, newarea->flags0,
			(uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_t *after = (area_t *)arealst;

	while (TRUE) {
		area_valid(after);

		if (area_is_guard(after->local.next))
			break;

		if (area_begining(newarea) < area_begining(after->local.next))
			break;

		after = after->local.next;
	}

	DEBUG("Will insert after $%.8x at $%.8x\n", (uint32_t)after, (uint32_t)area_begining(after));

	arealst_insert_area(arealst, after, newarea, DONTLOCK);

	if (locking)
		arealst_unlock(arealst);
}/*}}}*/

/**
 * Finds area on list that has size greater or equal to the given size Insert
 * <i>new area</i> after it.
 *
 * @param arealst
 * @param newarea
 * @return
 */

void arealst_insert_area_by_size(arealst_t *arealst, area_t *newarea, locking_t locking)/*{{{*/
{
	DEBUG("Will insert area at $%.8x to list at $%.8x %s locking\n",
			(uint32_t)newarea, (uint32_t)arealst, locking ? "with" : "without");

	if (locking)
		arealst_wrlock(arealst);

	area_t *after = (area_t *)arealst;

	while (TRUE) {
		area_valid(after);

		if (area_is_guard(after->local.next))
			break;

		if (after->size >= newarea->size)
			break;

		after = after->local.next;
	}

	arealst_insert_area(arealst, after, newarea, DONTLOCK);

	if (locking)
		arealst_unlock(arealst);
}/*}}}*/

/**
 * Removes the area from given list.
 * 
 * Procedure does not check if <i>area</i> is member of list.
 *
 * @param arealst
 * @param area
 * @return
 */

void arealst_remove_area(arealst_t *arealst, area_t *area, locking_t locking)/*{{{*/
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

	area->local.prev = NULL;
	area->local.next = NULL;

	area_touch(area);

	/* Decrement area counter of the list */
	arealst->areacnt--;

	if (locking)
		arealst_unlock(arealst);
}/*}}}*/

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

area_t *arealst_join_area(arealst_t *global, area_t *first, area_t *second, locking_t locking)/*{{{*/
{
	if (locking)
		arealst_wrlock(global);

	area_valid(first);
	area_valid(second);

	assert(area_is_used(first));
	assert(area_is_used(second));

	assert(area_end(first) == area_begining(second));

	/* Sum sizes */
	second->size += first->size;

	/* Remove first area from global list */
	area_valid(first->global.prev);

	first->global.prev->global.next = first->global.next;
	first->global.next->global.prev = first->global.prev;

	area_touch(first->global.prev);
	area_touch(first->global.next);

	if ((first->local.prev != NULL) && (first->local.next != NULL)) {
		second->local.prev = first->local.prev;
		second->local.next = first->local.next;

		second->local.prev->local.next = second;
		second->local.next->local.prev = second;
	}

	/* Invalidate removed area */
	memset(first, 0, sizeof(area_t));

	global->areacnt--;

	if (locking)
		arealst_unlock(global);

	return second;
}/*}}}*/

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

void arealst_split_area(arealst_t *global, area_t **splitted, area_t **remainder, uint32_t pages, locking_t locking)/*{{{*/
{
	if (locking)
		arealst_wrlock(global);

	area_t *area = *splitted;

	area_valid(area);
	assert(area_is_used(area));

	DEBUG("Will split area [$%.8x; %u; $%.2x] at $%.8x with cut point at $%.8x\n",
		  (uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area),
		  (uint32_t)area_begining(area) + pages * PAGE_SIZE);

	assert(pages * PAGE_SIZE < area->size);

	/* Now split point is inside area */
	area_t *newarea = area_footer(area_begining(area), pages);

	memset(newarea, 0, sizeof(area_t));

	/* set up new area */
	newarea->size	= pages * PAGE_SIZE;
	newarea->flags0	= area->flags0;

	newarea->global.next = area;
	newarea->global.prev = area->global.prev;

	if ((area->local.prev != NULL) && (area->local.next != NULL)) {
		newarea->local.prev = area->local.prev;
		newarea->local.next = area->local.next;

		area->local.prev->local.next = newarea;
		area->local.next->local.prev = newarea;
	}

	/* correct data in splitted area */
	area->size = area->size - pages * PAGE_SIZE;
	area->global.prev = newarea;

	area->local.prev = NULL;
	area->local.next = NULL;

	/* correct data in predecessor */
	newarea->global.prev->global.next = newarea;

	area_touch(newarea);
	area_touch(area);

	global->areacnt++;

	DEBUG("Area splitted to [$%.8x; %u; $%.2x] at $%.8x and [$%.8x; %u; $%.2x] at $%.8x\n",
		  (uint32_t)newarea, newarea->size, newarea->flags0, (uint32_t)area_begining(newarea),
		  (uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area));

	*splitted  = newarea;
	*remainder = area;

	assert(*splitted < *remainder);

	if (locking)
		arealst_unlock(global);
}/*}}}*/

/**
 * Internal procedure for area allocation. Pulls out area of at least size
 * <i>pages</i> from arealst. If <i>area</i> parameter is not null then
 * returned area will have that address.
 *
 * @param areamgr
 * @param area
 * @param pages
 * @return
 */

static area_t *arealst_pullout_area(arealst_t *arealst, area_t *addr, uint32_t pages, locking_t locking)/*{{{*/
{
	assert(pages > 0);

	if (locking)
		arealst_wrlock(arealst);

	area_t *area = NULL;

	if (arealst->areacnt > 0) {
		if (addr != NULL) {
			DEBUG("Seeking area of size %u pages at %.8x in list at %.8x\n",
				  pages, (uint32_t)addr, (uint32_t)arealst);

			area = (arealst_has_area(arealst, addr, DONTLOCK) && (addr->size >= pages * PAGE_SIZE)) ? addr : NULL;
		} else {
			DEBUG("Seeking area of size %u pages in list at %.8x\n", pages, (uint32_t)arealst);

			area = arealst_find_area_by_size(arealst, pages * PAGE_SIZE, DONTLOCK);
		}

		if (area != NULL) {
			DEBUG("Area found [$%.8x, %u, $%.2x] at $%.8x\n",
					(uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area));

			arealst_remove_area(arealst, area, DONTLOCK);
		}
	}

	if (locking)
		arealst_unlock(arealst);

	return area;
}/*}}}*/

/**
 * Takes an area and use its begining as space for area manager.
 *
 * @param area
 * @return
 */

areamgr_t *areamgr_init(area_t *area)/*{{{*/
{
	DEBUG("Using area at $%.8x [$%.8x; %u; $%.2x]\n", (uint32_t)area,
			(uint32_t)area_begining(area), area->size, area->flags0);

	area_valid(area);

	/* Check if area has enough space to hold area manager's structure */
	assert(area->size - sizeof(area_t) >= sizeof(areamgr_t));

	areamgr_t *areamgr = area_begining(area);

	/* Initialize free areas' lists */
	int32_t i;

	for (i = 0; i < AREAMGR_LIST_COUNT; i++)
		arealst_init(&areamgr->list[i]);

	/* Initialize global list */
	arealst_init(&areamgr->global);

	areamgr->global.global.next  = (area_t *)&areamgr->global;
	areamgr->global.global.prev  = (area_t *)&areamgr->global;
	areamgr->global.global_guard = TRUE;
	areamgr->global.areacnt = 1;
	area_touch((area_t *)&areamgr->global);

	areamgr->pagecnt = 0; /* SIZE_IN_PAGES(area->size); */

	DEBUG("Created area manager at $%.8x\n", (uint32_t)areamgr);

	return areamgr;
}/*}}}*/

/**
 * Adds new memory area to memory area manager.
 *
 * @param areamgr
 * @param newarea
 * @return
 */

void areamgr_add_area(areamgr_t *areamgr, area_t *newarea)/*{{{*/
{
	area_valid(newarea);

	DEBUG("Will add area [$%.8x; %u; $%.2x] to memory manager\n", (uint32_t)newarea, newarea->size, newarea->flags0);

	/* FIRST STEP: Insert onto all areas' list. */
	{
		arealst_wrlock(&areamgr->global);

		arealst_global_add_area(&areamgr->global, newarea, DONTLOCK);
		areamgr->pagecnt += SIZE_IN_PAGES(newarea->size);

		arealst_unlock(&areamgr->global);
	}

	/* SECOND STEP: Area is treated as it was used - so make it free */
	areamgr_free_area(areamgr, newarea);
}/*}}}*/

/**
 * Remove the area from the global list.
 *
 * The area must be marked as used. After that action, the area can be deleted.
 *
 * @param areamgr
 * @param area
 * @return
 */

void areamgr_remove_area(areamgr_t *areamgr, area_t *area)/*{{{*/
{
	area_valid(area);

	assert(!area_is_guard(area));
	assert(area_is_used(area));

	DEBUG("Remove area [$%.8x, %u, $%.2x] from the global list\n", (uint32_t)area, area->size, area->flags0);

	/* Remove it! */
	{
		arealst_wrlock(&areamgr->global);

		arealst_global_remove_area(&areamgr->global, area, DONTLOCK);
		areamgr->pagecnt -= SIZE_IN_PAGES(area->size);

		arealst_unlock(&areamgr->global);
	}
}/*}}}*/

/**
 * Allocates from area manager an area adjacent from left/right <i>side</i> to
 * area at <i>addr</i>  Area has to be of size <i>pages</i> or greater.
 *
 * Returned area is at least of size <i>pages</i>. Caller may want to shrink it
 * using <i>areamgr_shrink_area</i>.
 *
 * @param areamgr
 * @param addr
 * @param pages
 * @param side
 * @return
 */

area_t *areamgr_alloc_adjacent_area(areamgr_t *areamgr, area_t *addr, uint32_t pages, direction_t side)/*{{{*/
{
	DEBUG("Seeking area of size %u pages %s-adjacent to area [$%.8x, %u, $%.2x] at %.8x\n",
		  pages, (side == LEFT) ? "left" : "right",
		  (uint32_t)addr, addr->size, addr->flags0, (uint32_t)area_begining(addr));

	assert((side == LEFT) || (side == RIGHT));
	assert(pages > 0);

	area_t *area = NULL;

	bool alloc = TRUE;

	do {
		/* Check if next/previous area is adjacent */
		arealst_rdlock(&areamgr->global);

		if (side == RIGHT) {
			area = addr->global.next;

			alloc = ((area->size >= PAGE_SIZE * pages) && !area_is_global_guard(area) &&
					 !area_is_used(area) && (area_end(addr) == area_begining(area)));
		} else {
			area = addr->global.prev;

			alloc = ((area->size >= PAGE_SIZE * pages) && !area_is_global_guard(area) &&
					 !area_is_used(area) && (area_end(area) == area_begining(addr)));
		}

		if (alloc) {
			uint32_t n = SIZE_IN_PAGES(area->size) - 1;

			if (n > AREAMGR_LIST_COUNT - 1)
				n = AREAMGR_LIST_COUNT - 1;

			DEBUG("Area found [$%.8x, %u, $%.2x] at $%.8x\n",
					(uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area));

			area = arealst_pullout_area(&areamgr->list[n], area, pages, LOCK);
		} else {
			area = NULL;
		}

		arealst_unlock(&areamgr->global);
	} while (alloc && area == NULL);

	if (area != NULL) {
		areamgr->freecnt -= SIZE_IN_PAGES(area->size);
		area->used = TRUE;
		area_touch(area);

		DEBUG("Found area [$%.8x, %u, $%.2x] at $%.8x\n",
				(uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area));
	} else {
		DEBUG("Area not found!\n");
	}

	return area;
}/*}}}*/

/**
 * Allocates memory area from area manager. Allocated area will have exact size
 * of <i>pages</i>. If no area of satisfying size was found then call to the OS
 * will be done in order to obtain new pages.
 *
 * @param areamgr
 * @param pages
 * @return			area of exact size <i>pages</i> or NULL
 */

area_t *areamgr_alloc_area(areamgr_t *areamgr, uint32_t pages)/*{{{*/
{
	DEBUG("Will try to find area of size %u pages\n", pages);

	assert(pages > 0);

	int32_t n = pages - 1;

	if (n > AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT - 1;

	/* browse through lists till proper area is not found */
	area_t *area = NULL;

	while (n < AREAMGR_LIST_COUNT) {
		if ((area = arealst_pullout_area(&areamgr->list[n], NULL, pages, LOCK)))
			break;

		n++;
	}

	/* If area was found then reserve it */
	if (area != NULL) {
		areamgr->freecnt -= SIZE_IN_PAGES(area->size);
		area->used = TRUE;
		area_touch(area);

		DEBUG("Found area [$%.8x, %u, $%.2x] at $%.8x\n",
				(uint32_t)area, area->size, area->flags0, (uint32_t)area_begining(area));

		/* If area is too big it should be shrinked */
		if (area->size > pages * PAGE_SIZE)
			areamgr_shrink_area(areamgr, &area, pages, RIGHT);
	} else {
		DEBUG("Area not found - will create one!\n");

		if ((area = area_new(PM_MMAP, pages))) {
			arealst_global_add_area(&areamgr->global, area, LOCK);
			areamgr->pagecnt += SIZE_IN_PAGES(area->size);
		}
	}

	return area;
}/*}}}*/

/**
 * Put some pages on free list if there are no free pages.
 */

bool areamgr_prealloc_area(areamgr_t *areamgr, uint32_t pages)/*{{{*/
{
	area_t *newarea = NULL;

	arealst_wrlock(&areamgr->global);

	if (areamgr->freecnt == 0) {
		DEBUG("Will prealloc area of size %u pages.\n", pages);

		if ((newarea = area_new(PM_MMAP, pages)))
			arealst_global_add_area(&areamgr->global, newarea, DONTLOCK);

		areamgr->freecnt += SIZE_IN_PAGES(newarea->size);
		areamgr->pagecnt += SIZE_IN_PAGES(newarea->size);
		newarea->used = FALSE;
		area_touch(newarea);
	}

	arealst_unlock(&areamgr->global);

	if (newarea != NULL) {
		/* qualify area for insertion into proper free-list */
		int n = SIZE_IN_PAGES(newarea->size) - 1;

		if (n >= AREAMGR_LIST_COUNT - 1)
			n = AREAMGR_LIST_COUNT - 1;

		/* insert area on proper free list */
		arealst_insert_area_by_size(&areamgr->list[n], newarea, LOCK);
	}

	return (newarea != NULL);
}/*}}}*/

/**
 * Frees memory area for use by area manager.
 *
 * Idea for caller: After freeing some pages a threshold (unused pages count)
 * can be exceeded triggering a few pages being returned to the OS.
 *
 * @param areamgr
 * @param newarea
 */

void areamgr_free_area(areamgr_t *areamgr, area_t *newarea)/*{{{*/
{
	DEBUG("Will try to free area [$%.8x, %u, $%.2x] at $%.8x\n",
			(uint32_t)newarea, newarea->size, newarea->flags0, (uint32_t)area_begining(newarea));

	assert(area_is_used(newarea));

	/* mark area as free */
	{
		area_t *prev = areamgr_alloc_adjacent_area(areamgr, newarea, 1, LEFT);
		area_t *next = areamgr_alloc_adjacent_area(areamgr, newarea, 1, RIGHT);

		arealst_wrlock(&areamgr->global);

		if (prev != NULL) {
			DEBUG("Coalescing with left neighbour [$%.8x; $%x; $%.2x]\n",
					(uint32_t)prev, prev->size, prev->flags0);

			newarea = arealst_join_area(&areamgr->global, prev, newarea, DONTLOCK);

			DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)newarea, newarea->size, newarea->flags0);
		}

		if (next != NULL) {
			DEBUG("Coalescing with right neighbour [$%.8x; $%x; $%.2x]\n",
					(uint32_t)next, next->size, next->flags0);

			newarea = arealst_join_area(&areamgr->global, newarea, next, DONTLOCK);

			DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)newarea, newarea->size, newarea->flags0);
		}

		newarea->used = FALSE;
		newarea->manager = AREA_MGR_UNMANAGED;
		area_touch(newarea);

		areamgr->freecnt += SIZE_IN_PAGES(newarea->size);

		arealst_unlock(&areamgr->global);
	}

	/* qualify area for insertion into proper free-list */
	int n = SIZE_IN_PAGES(newarea->size) - 1;

	if (n >= AREAMGR_LIST_COUNT - 1)
		n = AREAMGR_LIST_COUNT - 1;

	/* insert area on proper free list */
	arealst_insert_area_by_size(&areamgr->list[n], newarea, LOCK);
}/*}}}*/

/**
 * Coalesces memory area with adjacent areas.
 *
 * WARNING: This function is quite useless now... and undeniably it's buggy.
 *
 * @param aremgr
 * @param area
 * @param direction
 * @return
 */

area_t *areamgr_coalesce_area(areamgr_t *areamgr, area_t *area)/*{{{*/
{
	area_valid(area);
	assert(area_is_used(area));

	DEBUG("Will try to coalesce area [$%.8x; $%x; $%.2x] with adjacent areas\n",
		  (uint32_t)area, area->size, area->flags0);

	area_valid(area->global.next);
	area_valid(area->global.prev);

	/* coalesce with global.next area */
	while (!area_is_guard(area->global.next) && !area_is_used(area->global.next) &&
		   ((void *)area + sizeof(area_t) == area_begining(area->global.next)))
	{
		DEBUG("Coalescing with right neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->global.next, area->global.next->size, area->global.next->flags0);

		area = arealst_join_area(&areamgr->global, area,
								 areamgr_alloc_adjacent_area(areamgr, area, 1, RIGHT), DONTLOCK);

		DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags0);
	}

	/* coalesce with previous area */
	while (!area_is_guard(area->global.prev) && !area_is_used(area->global.prev) &&
		((void *)area->global.prev + sizeof(area_t) == area_begining(area)))
	{
		DEBUG("Coalescing with left neighbour [$%.8x; $%x; $%.2x]\n",
			  (uint32_t)area->global.prev, area->global.prev->size, area->global.prev->flags0);

		area = arealst_join_area(&areamgr->global, areamgr_alloc_adjacent_area(areamgr, area, 1, LEFT),
								 area, DONTLOCK);

		DEBUG("Coalesced into area [$%.8x; $%x; $%.2x]\n", (uint32_t)area, area->size, area->flags0);
	}

	areamgr_free_area(areamgr, area);

	return area;
}/*}}}*/

/**
 * Expands area from given <i>side</i>. Area will be expanded only if there is
 * proper free area within area manager (no new pages will be taken from OS).
 *
 * If function returned TRUE then area was expanded to size <i>pages</i> or
 * greater. Caller may want to shrink area if it's too big.
 *
 * @param areamgr
 * @param area		pointer to expanded area, if call was successful it will
 *					contain area of size <i>pages</i> or greater
 * @param pages
 * @param side
 * @return
 */

bool areamgr_expand_area(areamgr_t *areamgr, area_t **area, uint32_t pages, direction_t side)/*{{{*/
{
	area_t *newarea = *area;

	area_valid(newarea);

	assert(pages > 0);
	assert(area_is_used(newarea));

	DEBUG("Will expand area at $%.8x [$%.8x; %u; $%.2x] by %u pages from %s side.\n",
		  (uint32_t)newarea, (uint32_t)area_begining(newarea), newarea->size, newarea->flags0,
		  pages, (side == LEFT) ? "left" : "right");

	area_t *expansion = areamgr_alloc_adjacent_area(areamgr, newarea, pages, side);

	arealst_wrlock(&areamgr->global);

	uint8_t manager = newarea->manager;
	bool    ready   = newarea->ready;

	if (expansion != NULL) {
		if (side == RIGHT)
			newarea = arealst_join_area(&areamgr->global, newarea, expansion, DONTLOCK);
		else
			newarea = arealst_join_area(&areamgr->global, expansion, newarea, DONTLOCK);
	}

	newarea->manager = manager;
	newarea->ready   = ready;
	area_touch(newarea);

	arealst_unlock(&areamgr->global);

	DEBUG("Area at $%.8x expanded to [$%.8x; %u; $%.2x]\n",
			(uint32_t)newarea, (uint32_t)area_begining(newarea), newarea->size, newarea->flags0);

	*area = newarea;

	return expansion != NULL;
}/*}}}*/

/**
 * Shrinks area from given <i>side</i> - splits it and inserts leftover to area
 * manager. Shrinked area will have exact size of <i>pages</i>.
 *
 * If area is shrinked from the right side it means that its begining won't
 * be moved. If from the left side then its end.
 *
 * @param areamgr
 * @param area		pointer to an area which will be shrinked
 * @param pages
 * @param side
 */

void areamgr_shrink_area(areamgr_t *areamgr, area_t **area, uint32_t pages, direction_t side)/*{{{*/
{
	area_t *newarea = *area;

	area_valid(newarea);

	assert(pages > 0);
	assert((side == LEFT) || (side == RIGHT));
	assert(area_is_used(newarea));

	DEBUG("Will %s-shrink area at $%.8x [$%.8x; %u; $%.2x] by %u pages\n", (side == LEFT) ? "left" : "right", 
		  (uint32_t)newarea, (uint32_t)area_begining(newarea), newarea->size, newarea->flags0,
		  SIZE_IN_PAGES(newarea->size) - pages);

	area_t *leftover = newarea;

	if (side == RIGHT) {
		arealst_split_area(&areamgr->global, &newarea, &leftover, pages, LOCK);
	} else {
		arealst_wrlock(&areamgr->global);

		arealst_split_area(&areamgr->global, &leftover, &newarea, SIZE_IN_PAGES(newarea->size) - pages, DONTLOCK);

		if ((leftover->local.prev != NULL) && (leftover->local.next != NULL)) {
			newarea->local.prev = leftover->local.prev;
			newarea->local.next = leftover->local.next;

			newarea->local.prev->local.next = newarea;
			newarea->local.next->local.prev = newarea;
		}
	
		newarea->manager = leftover->manager;
		newarea->ready   = leftover->ready;

		arealst_unlock(&areamgr->global);
	}

	areamgr_free_area(areamgr, leftover);

	DEBUG("Area at $%.8x shrinked to [$%.8x; %u; $%.2x]\n",
			(uint32_t)newarea, (uint32_t)area_begining(newarea), newarea->size, newarea->flags0);

	*area = newarea;
}/*}}}*/

