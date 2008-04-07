/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Page manager -- sbrk emulation through implementation in
 * 			constant-sized shared memory area.
 */

#if !defined DBG_SYSMEM && !defined NDEBUG
#define NDEBUG
#endif

#include "sysmem.h"

#include <sys/mman.h>
#include <unistd.h>

#ifndef PM_PAGES
#define PM_PAGES	8192		/* 32 MiB */
#endif

static struct {
	uint8_t *start;
	uint8_t *brk;
	uint8_t *end;
} pages;

void pm_shm_init()
{
	pages.start	= (uint8_t*) mmap(0, PAGE_SIZE * PM_PAGES, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
	pages.brk	= pages.start;
	pages.end	= pages.start + PAGE_SIZE * PM_PAGES;

	assert(pages.start != NULL);
}

void *pm_shm_alloc(void *hint, uint32_t n)
{
	void *area = pages.brk;

	if (pages.brk + (n * PAGE_SIZE) > pages.end)
		return NULL;

	pages.brk += n * PAGE_SIZE;

	return area;
}

bool pm_shm_free(void *area, uint32_t n)
{
	if ((uint8_t *)area + (PAGE_SIZE * n) == pages.brk) {
		pages.brk -= PAGE_SIZE * n;

		return TRUE;
	}

	return FALSE;
}
