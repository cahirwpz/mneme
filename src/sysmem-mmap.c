/*
 * Author:	Krystian Bacławski <krystian.baclawski@gmail.com>
 *
 * Page manager: mmap implementation.
 */

#ifndef DBG_SYSMEM
#define NDEBUG
#endif

#include "sysmem.h"

#include <sys/mman.h>
#include <unistd.h>

void pm_mmap_init()
{
}

void *pm_mmap_alloc(uint32_t n)
{
	void *area = mmap(0, PAGE_SIZE * n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);

	return (area != (void *)-1) ? (area) : (NULL);
}

bool pm_mmap_free(void *start, uint32_t n)
{
	return munmap(start, PAGE_SIZE * n);
}
