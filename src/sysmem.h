#ifndef __SYS_H
#define __SYS_H

#include "common.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif

#define SIZE_IN_PAGES(size)		(ALIGN(size, PAGE_SIZE) / PAGE_SIZE)

typedef enum { PM_SBRK, PM_MMAP, PM_SHM } pm_type_t;

void pm_mmap_init();
void *pm_mmap_alloc(uint32_t n);
bool pm_mmap_free(void *area, uint32_t n);

void pm_sbrk_init();
void *pm_sbrk_alloc(uint32_t n);
bool pm_sbrk_free(void *area, uint32_t n);

void pm_shm_init();
void *pm_shm_alloc(uint32_t n);
bool pm_shm_free(void *area, uint32_t n);

#endif
