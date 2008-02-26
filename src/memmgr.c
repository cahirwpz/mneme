#include "blkmgr.h"
#include "eqsbmgr.h"
#include "mmapmgr.h"
#include "memmgr.h"

#define PROCNUM	1

/**
 * Memory manager initialization.
 */

memmgr_t *memmgr_init()/*{{{*/
{
	uint32_t memmgr_size = sizeof(memmgr_t) + sizeof(percpumgr_t) * PROCNUM;

	memmgr_t *memmgr = (memmgr_t *)areamgr_init(area_new(PM_MMAP, SIZE_IN_PAGES(memmgr_size)));

	int i;
	
	for (i = 0; i < PROCNUM; i++) {
		mmapmgr_init(&memmgr->percpumgr[0].mmapmgr, &memmgr->areamgr);
		blkmgr_init(&memmgr->percpumgr[0].blkmgr, &memmgr->areamgr);
		eqsbmgr_init(&memmgr->percpumgr[0].eqsbmgr, &memmgr->areamgr);
	}

	return memmgr;
}/*}}}*/

/**
 * Allocate memory block.
 */

void *memmgr_alloc(memmgr_t *memmgr, uint32_t size, uint32_t alignment)/*{{{*/
{
	if (alignment) {
		DEBUG("\033[37;1mRequested block of size %u aligned to %u bytes boundary.\033[0m\n", size, alignment);
	} else {
		DEBUG("\033[37;1mRequested block of size %u.\033[0m\n", size);
	}

	void *memory;

	if (size == 0)
		memory = NULL;
	else if (size <= 32)
		memory = eqsbmgr_alloc(&memmgr->percpumgr[0].eqsbmgr, size, 0);
	else if (size < 32767)
		memory = blkmgr_alloc(&memmgr->percpumgr[0].blkmgr, size, alignment);
	else
		memory = mmapmgr_alloc(&memmgr->percpumgr[0].mmapmgr, size, alignment);

	if (memory) {
		DEBUG("\033[37;1mBlock found at $%.8x.\033[0m\n", (uint32_t)memory);
	} else {
		DEBUG("\033[37;1mBlock not found!.\033[0m\n");
	}

	return memory;
}/*}}}*/

/**
 * Reallocate memory block.
 */

bool memmgr_realloc(memmgr_t *self, void *memory, uint32_t new_size)/*{{{*/
{
	int8_t mgrtype = 0;

	/* find to which area the block belongs */
	arealst_rdlock(&self->areamgr.global);

	area_t *area = (area_t *)self->areamgr.global.global.next;

	while (!area->global_guard) {
		if ((area_begining(area) <= memory) && (memory < area_end(area)))
			break;

		area = area->global.next;
	}

	mgrtype = (area != NULL) ? area->manager : -1;

	arealst_unlock(&self->areamgr.global);

	/* redirect free request to proper manager */
	bool res = FALSE;

	switch (mgrtype)
	{
		case AREA_MGR_EQSBMGR:
			res = eqsbmgr_realloc(&self->percpumgr[0].eqsbmgr, memory, new_size);
			break;

		case AREA_MGR_BLKMGR:
			res = blkmgr_realloc(&self->percpumgr[0].blkmgr, memory, new_size);
			break;

		case AREA_MGR_MMAPMGR:
			res = mmapmgr_realloc(&self->percpumgr[0].mmapmgr, memory, new_size);
			break;

		case AREA_MGR_UNMANAGED:
			DEBUG("Area is not managed by any sub-allocator (id: %u)!\n", area->manager);
			break;

		default:
			DEBUG("Area does not exists ?!\n");
			break;
	}

	return res;
}/*}}}*/

/**
 * Free memory block.
 */

bool memmgr_free(memmgr_t *self, void *memory)/*{{{*/
{
	DEBUG("\033[37;1mRequested to free block at $%.8x.\033[0m\n", (uint32_t)memory);

	int8_t mgrtype = 0;

	/* find to which area the block belongs */
	arealst_rdlock(&self->areamgr.global);

	area_t *area = (area_t *)self->areamgr.global.global.next;

	while (!area->global_guard) {
		if ((area_begining(area) <= memory) && (memory < area_end(area)))
			break;

		area = area->global.next;
	}

	mgrtype = (area != NULL) ? area->manager : -1;

	arealst_unlock(&self->areamgr.global);

	/* redirect free request to proper manager */
	bool res = FALSE;

	switch (mgrtype)
	{
		case AREA_MGR_EQSBMGR:
			res = eqsbmgr_free(&self->percpumgr[0].eqsbmgr, memory);
			break;

		case AREA_MGR_BLKMGR:
			res = blkmgr_free(&self->percpumgr[0].blkmgr, memory);
			break;

		case AREA_MGR_MMAPMGR:
			res = mmapmgr_free(&self->percpumgr[0].mmapmgr, memory);
			break;

		case AREA_MGR_UNMANAGED:
			DEBUG("Area is not managed by any sub-allocator (id: %u)!\n", area->manager);
			break;

		default:
			DEBUG("Area does not exists ?!\n");
			break;
	}

	/* fprintf(stderr, "pagecnt = %u, freecnt = %u\n", self->areamgr.pagecnt, self->areamgr.freecnt); */

	return res;
}/*}}}*/

/**
 * Print memory manager structures.
 */

void memmgr_print(memmgr_t *memmgr)/*{{{*/
{
	arealst_rdlock(&memmgr->areamgr.global);

	fprintf(stderr, "\033[1;37mPrinting memory manager structures:\033[0m\n");

	fprintf(stderr, "\033[1;35m areamgr at $%.8x [%d areas, %u pages free]:\033[0m\n",
			(uint32_t)&memmgr->areamgr, memmgr->areamgr.global.areacnt, memmgr->areamgr.freecnt);

	area_t *area = (area_t *)&memmgr->areamgr.global;

	bool error = FALSE;
	uint32_t areacnt = 1;
	uint32_t freepages = 0;

	while (TRUE) {
		area_valid(area);

		if (!area->guard) {
			fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x : %8d : %d\033[0m\n", area->used ? '1' : '2',
					(uint32_t)area_begining(area), (uint32_t)area_end(area), area->size, area->manager);
			if (!area->used)
				freepages += SIZE_IN_PAGES(area->size);
		} else {
			fprintf(stderr, "\033[1;33m  $%.8x %11s : %8s\033[0m\n", (uint32_t)area, "", "guard");
		}

		if (area->global.next->global_guard)
			break;

		if (!area->global_guard && (area >= area->global.next))
			error = TRUE;

		area = area->global.next;

		areacnt++;
	}

	assert(!error);
	assert(areacnt == memmgr->areamgr.global.areacnt);
	assert(freepages == memmgr->areamgr.freecnt);

	arealst_unlock(&memmgr->areamgr.global);

	mmapmgr_print(&memmgr->percpumgr[0].mmapmgr);
	blkmgr_print(&memmgr->percpumgr[0].blkmgr);
	eqsbmgr_print(&memmgr->percpumgr[0].eqsbmgr);
}/*}}}*/

