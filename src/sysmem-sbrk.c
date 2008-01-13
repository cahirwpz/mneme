/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Page manager -- sbrk implementation
 */

#if !defined DBG_SYSMEM && !defined NDEBUG
#define NDEBUG
#endif

#include "sysmem.h"

#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

void pm_sbrk_init()
{
	DEBUG("segment end: $%.8x\n", (uint32_t)sbrk(0));
}

void *pm_sbrk_alloc(uint32_t n)
{
	DEBUG("segment end: $%.8x\n", (uint32_t)sbrk(0));

	void *area = sbrk(n * PAGE_SIZE);

	DEBUG("segment extended to: $%.8x\n", (uint32_t)sbrk(0));

	return (area != (void *)-1) ? (area) : (NULL);
}

bool pm_sbrk_free(void *area, uint32_t n)
{
	uint8_t *end = (uint8_t *) sbrk(0);

	DEBUG("segment end: $%.8x\n", (uint32_t)sbrk(0));

	if ((uint8_t *)area + (PAGE_SIZE * n) == end) {
		if (brk(area) == 0) {
			DEBUG("segment shrinked to: $%.8x\n", (uint32_t)sbrk(0));

			return TRUE;
		}
	}

	return FALSE;
}
