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

			I(((uint32_t)area_begining(area) & (alignment-1)) == 0);

			if (SIZE_IN_PAGES(area->size) > SIZE_IN_PAGES(size)) {
				DEBUG("Will cut %u pages from back\n", area->size - SIZE_IN_PAGES(size));

				arealst_split_area(&mmapmgr->areamgr->global, &area, &leftover, SIZE_IN_PAGES(size), LOCK);

				areamgr_free_area(mmapmgr->areamgr, leftover);
			}

			I(SIZE_IN_PAGES(area->size) == SIZE_IN_PAGES(size));
		}

		arealst_wrlock(&mmapmgr->blklst);

		area->manager = AREA_MGR_MMAPMGR;
		area_touch(area);

		arealst_insert_area_by_addr(&mmapmgr->blklst, (void *)area, DONTLOCK);

		arealst_unlock(&mmapmgr->blklst);

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

	bool res = FALSE;

	if (area != NULL) {
		uint32_t newsize = SIZE_IN_PAGES(size + sizeof(area_t));
		uint32_t oldsize = SIZE_IN_PAGES(area->size);

		if (newsize == oldsize) {
			res = TRUE;
		} else {
			DEBUG("Resizing from %u to %u pages!\n", oldsize, newsize);

			if (newsize < oldsize) {
				areamgr_shrink_area(mmapmgr->areamgr, &area, newsize, RIGHT);
				res = TRUE;
			} else {
				if (areamgr_expand_area(mmapmgr->areamgr, &area, newsize - oldsize, RIGHT)) {
					if (SIZE_IN_PAGES(area->size) > newsize)
						areamgr_shrink_area(mmapmgr->areamgr, &area, newsize, RIGHT);

					area->manager = AREA_MGR_MMAPMGR;
					area_touch(area);

					res = TRUE;
				}
			}

			if (res) {
				DEBUG("Resized block [$%.8x; %u; $%.2x]\n", (uint32_t)area_begining(area), area->size, area->flags0);
			} else {
				DEBUG("Cannot resize!\n");
			}
		}
	}

	arealst_unlock(&mmapmgr->blklst);

	return res;
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

	if (area != NULL) {
		arealst_remove_area(&mmapmgr->blklst, area, DONTLOCK);
		areamgr_free_area(mmapmgr->areamgr, area);
	}

	arealst_unlock(&mmapmgr->blklst);

	DEBUG("Area at $%.8x %sfreed!\n", (uint32_t)memory, (area) ? "" : "not ");

	return (area) ? TRUE : FALSE;
}/*}}}*/

/**
 *
 * @param mmapmgr
 */

bool mmapmgr_verify(mmapmgr_t *mmapmgr, bool verbose)/*{{{*/
{
	bool error = FALSE;

	arealst_rdlock(&mmapmgr->blklst);

	if (verbose)
		fprintf(stderr, "\033[1;36m mmapmgr at $%.8x [%d areas]:\033[0m\n",
				(uint32_t)mmapmgr, mmapmgr->blklst.areacnt);

	area_t *blk = (area_t *)&mmapmgr->blklst;

	uint32_t blkcnt = 0;

	while (TRUE) {
		area_valid(blk);

		if (verbose) {
			if (!area_is_guard(blk))
				fprintf(stderr, "\033[1;3%dm  $%.8x - $%.8x: %8d : $%.8x : $%.8x\033[0m\n",
						(blk->manager == AREA_MGR_MMAPMGR),
						(uint32_t)area_begining(blk), (uint32_t)area_end(blk), blk->size,
						(uint32_t)blk->local.prev, (uint32_t)blk->local.next);
			else
				fprintf(stderr, "\033[1;33m  $%.8x %11s: %8s : $%.8x : $%.8x\033[0m\n",
						(uint32_t)blk, "", "guard", (uint32_t)blk->local.prev, (uint32_t)blk->local.next);
		}

		if (area_is_guard(blk->local.next))
			break;

		error |= (!area_is_guard(blk) && (blk >= blk->local.next));

		blk = blk->local.next;

		blkcnt++;
	}

	error |= (blkcnt != mmapmgr->blklst.areacnt);

	if (error && verbose)
		fprintf(stderr, "\033[7m  Invalid!\033[0m\n");

	arealst_unlock(&mmapmgr->blklst);

	return error;
}/*}}}*/

