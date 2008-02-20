/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Manager for large blocks.
 */

#include "areamgr.h"
#include "mmapmgr.h"

/**
 * Initializes mmapmgr manager structure.
 *
 * @param mmapmgr
 * @param areamgr
 */

void mmapmgr_init(mmapmgr_t *mmapmgr, areamgr_t *areamgr)/*{{{*/
{
	arealst_init(&mmapmgr->blklst);

	mmapmgr->areamgr = areamgr;
}/*}}}*/

/**
 *
 * @param mmapmgr
 * @param size
 * @param alignment
 * @return
 */

void *mmapmgr_alloc(mmapmgr_t *mmapmgr, uint32_t size, uint32_t alignment)/*{{{*/
{
	DEBUG("Requested to allocate block of size %u with alignment $%x\n", size, alignment);

	size += sizeof(area_t);

	if (alignment <= PAGE_SIZE)
		alignment = 0;

	area_t *area = areamgr_alloc_area(mmapmgr->areamgr, SIZE_IN_PAGES(size) + SIZE_IN_PAGES(alignment));

	if (area != NULL) {
		DEBUG("Found block at $%.8x\n", (uint32_t)area_begining(area));

		if (alignment > 0) {
			area_t *leftover = NULL;

			uint32_t excess = ((uint32_t)area_begining(area) & (alignment - 1));

			if (excess > 0) {
				excess = alignment - excess;

				DEBUG("Will cut %u pages from front\n", SIZE_IN_PAGES(excess));

				arealst_split_area(&mmapmgr->areamgr->global, &area, &leftover, SIZE_IN_PAGES(excess), LOCK);

				areamgr_free_area(mmapmgr->areamgr, area);

				area = leftover;
			}

			assert(((uint32_t)area_begining(area) & (alignment-1)) == 0);

			if (SIZE_IN_PAGES(area->size) > SIZE_IN_PAGES(size)) {
				DEBUG("Will cut %u pages from back\n", area->size - SIZE_IN_PAGES(size));

				arealst_split_area(&mmapmgr->areamgr->global, &area, &leftover, SIZE_IN_PAGES(size), LOCK);

				areamgr_free_area(mmapmgr->areamgr, leftover);
			}

			assert(SIZE_IN_PAGES(area->size) == SIZE_IN_PAGES(size));
		}

		arealst_insert_area_by_addr(&mmapmgr->blklst, (void *)area, LOCK);

		DEBUG("Will use block [$%.8x; %u; $%.2x]\n", (uint32_t)area_begining(area), area->size, area->flags0);
	}

	return area ? area_begining(area) : NULL;
}/*}}}*/

/**
 *
 * @param mmapmgr
 * @param memory
 * @param newsize
 * @return
 */

bool mmapmgr_realloc(mmapmgr_t *mmapmgr, void *memory, uint32_t size)/*{{{*/
{
	DEBUG("Requested to resize block at $%.8x to size %u\n", (uint32_t)memory, size);

	arealst_wrlock(&mmapmgr->blklst);

	area_t *area = arealst_find_area_by_addr(&mmapmgr->blklst, memory, DONTLOCK);

	arealst_unlock(&mmapmgr->blklst);

	if (area != NULL) {
		uint32_t newsize = SIZE_IN_PAGES(size + sizeof(area_t));
		uint32_t oldsize = SIZE_IN_PAGES(area->size);

		if (newsize == oldsize)
			return TRUE;

		DEBUG("Resizing from %u to %u pages!\n", oldsize, newsize);

		if (newsize < oldsize)
			areamgr_shrink_area(mmapmgr->areamgr, &area, oldsize - newsize, RIGHT);

		if (newsize > oldsize) {
			if (!areamgr_expand_area(mmapmgr->areamgr, &area, newsize - oldsize, RIGHT))
				area = NULL;
		}

		if (area != NULL) {
			DEBUG("Resized block [$%.8x; %u; $%.2x]\n", (uint32_t)area_begining(area), area->size, area->flags0);
		} else {
			DEBUG("Cannot resize!\n");
		}
	}

	return (area != NULL);
}/*}}}*/

/**
 *
 * @param mmapmgr
 * @param memory
 * @return
 */

bool mmapmgr_free(mmapmgr_t *mmapmgr, void *memory)/*{{{*/
{
	DEBUG("Requested to free block at $%.8x\n", (uint32_t)memory);

	arealst_wrlock(&mmapmgr->blklst);

	area_t *area = arealst_find_area_by_addr(&mmapmgr->blklst, memory, DONTLOCK);

	if (area != NULL)
		arealst_remove_area(&mmapmgr->blklst, area, DONTLOCK);

	arealst_unlock(&mmapmgr->blklst);

	if (area != NULL)
		areamgr_free_area(mmapmgr->areamgr, area);

	DEBUG("Area at $%.8x %sfreed!\n", (uint32_t)memory, (area) ? "" : "not ");

	return (area) ? TRUE : FALSE;
}/*}}}*/

/**
 *
 * @param mmapmgr
 */

void mmapmgr_print(mmapmgr_t *mmapmgr)/*{{{*/
{
	arealst_rdlock(&mmapmgr->blklst);

	fprintf(stderr, "\033[1;36m mmapmgr at $%.8x [%d areas]:\033[0m\n",
			(uint32_t)mmapmgr, mmapmgr->blklst.areacnt);

	area_t *blk = (area_t *)&mmapmgr->blklst;

	bool error = FALSE;
	uint32_t blkcnt = 0;

	while (TRUE) {
		area_valid(blk);

		if (!area_is_guard(blk))
			fprintf(stderr, "\033[1;31m  $%.8x - $%.8x: %8d : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area_begining(blk), (uint32_t)area_end(blk), blk->size,
					(uint32_t)blk->local.prev, (uint32_t)blk->local.next);
		else
			fprintf(stderr, "\033[1;33m  $%.8x %11s: %8s : $%.8x : $%.8x\033[0m\n",
					(uint32_t)blk, "", "guard", (uint32_t)blk->local.prev, (uint32_t)blk->local.next);

		if (area_is_guard(blk->local.next))
			break;

		if (!area_is_guard(blk) && (blk >= blk->local.next))
			error = TRUE;

		blk = blk->local.next;

		blkcnt++;
	}

	assert(!error);

	assert(blkcnt == mmapmgr->blklst.areacnt);

	arealst_unlock(&mmapmgr->blklst);
}/*}}}*/

