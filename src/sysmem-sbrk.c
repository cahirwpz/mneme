/*
 * Author:	Krystian Bacławski <krystian.baclawski@gmail.com>
 *
 * Page manager: sbrk implementation.
 */

#define NDEBUG

#include "sysmem.h"

#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

void pm_sbrk_init()
{
	uint8_t *end = (uint8_t *) sbrk(0);

	DEBUG("segment end: $%.8x\n", (uint32_t)sbrk(0));

	/* align end of bss segment to page size */
	assert(sbrk(PAGE_SIZE - ((uint32_t)end & (PAGE_SIZE - 1))) != (void *)-1);
	
	DEBUG("segment extended to: $%.8x\n", (uint32_t)sbrk(0));
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
		sbrk(-PAGE_SIZE * n);

		DEBUG("segment shrinked to: $%.8x\n", (uint32_t)sbrk(0));

		return TRUE;
	}

	return FALSE;
}
