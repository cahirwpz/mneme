#include "blkmgr.h"
#include "mmapmgr.h"
#include "memmgr.h"

#define MANAGER 1

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
	}

	return memmgr;
}/*}}}*/

/**
 * Allocate memory block.
 */

void *memmgr_alloc(memmgr_t *memmgr, uint32_t size, uint32_t alignment)/*{{{*/
{
#if MANAGER == 1
	return mmapmgr_alloc(&memmgr->percpumgr[0].mmapmgr, size, alignment);
#elif MANAGER == 2
	return blkmgr_alloc(&memmgr->percpumgr[0].blkmgr, size, alignment);
#endif
}/*}}}*/

/**
 * Reallocate memory block.
 */

bool memmgr_realloc(memmgr_t *memmgr, void *memory, uint32_t new_size)/*{{{*/
{
#if MANAGER == 1
	return mmapmgr_realloc(&memmgr->percpumgr[0].mmapmgr, memory, new_size);
#elif MANAGER == 2
	return blkmgr_realloc(&memmgr->percpumgr[0].blkmgr, memory, new_size);
#endif
}/*}}}*/

/**
 * Free memory block.
 */

bool memmgr_free(memmgr_t *memmgr, void *memory)/*{{{*/
{
#if MANAGER == 1
	return mmapmgr_free(&memmgr->percpumgr[0].mmapmgr, memory);
#elif MANAGER == 2
	return blkmgr_free(&memmgr->percpumgr[0].blkmgr, memory);
#endif
}/*}}}*/

/**
 * Print memory manager structures.
 */

void memmgr_print(memmgr_t *memmgr)/*{{{*/
{
	arealst_rdlock(&memmgr->areamgr.global);

	fprintf(stderr, "\033[1;36m areamgr at $%.8x [%d areas]:\033[0m\n",
			(uint32_t)&memmgr->areamgr, memmgr->areamgr.global.areacnt);

	area_t *area = (area_t *)&memmgr->areamgr.global;

	bool error = FALSE;
	uint32_t areacnt = 1;

	while (TRUE) {
		area_valid(area);

		if (!area_is_guard(area))
			fprintf(stderr, "\033[1;3%cm  $%.8x - $%.8x: %8d\033[0m\n", area_is_used(area) ? '1' : '2',
					(uint32_t)area_begining(area), (uint32_t)area_end(area), area->size);
		else
			fprintf(stderr, "\033[1;33m  $%.8x %11s: %8s\033[0m\n", (uint32_t)area, "", "guard");

		if (area_is_global_guard(area->global.next))
			break;

		if (!area_is_global_guard(area) && (area >= area->global.next))
			error = TRUE;

		area = area->global.next;

		areacnt++;
	}

	assert(!error);
	assert(areacnt == memmgr->areamgr.global.areacnt);

	arealst_unlock(&memmgr->areamgr.global);

	mmapmgr_print(&memmgr->percpumgr[0].mmapmgr);
	blkmgr_print(&memmgr->percpumgr[0].blkmgr);
}/*}}}*/

