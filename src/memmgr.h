#ifndef __MEMMGR_H
#define __MEMMGR_H

#include "common.h"
#include "sysmem.h"
#include <stdio.h>

/* Memory area structure */

struct memarea				/* size of this structure will be aligned to 16 bytes boundary */
{
	uint16_t checksum;
	uint16_t flags;	
	uint32_t size;

	struct memarea *prev;
	struct memarea *next;
} __attribute__((aligned(16)));

typedef struct memarea memarea_t;

/* Memory manager structure */

struct memmgr
{
	struct memarea *areas;
};

typedef struct memmgr memmgr_t;

/* Flags definition */

#define MA_FLAG_READY	1
#define MA_FLAG_MMAP	2
#define MA_FLAG_SBRK	4
#define MA_FLAG_SHM		8

/* Checksum functions for memory area structure */

static inline uint16_t ma_checksum(memarea_t *area)
{
	int bytes = sizeof(memarea_t) - sizeof(uint16_t);

	return (((uint32_t)area) >> 16) ^ (((uint32_t)area) & 0xFFFF) ^ checksum((uint16_t *)&area->flags, bytes >> 1);
}

static inline void ma_touch(memarea_t *area)
{
	area->checksum = ma_checksum(area);
}

static inline void ma_valid(memarea_t *area)
{
	if (ma_checksum(area) != area->checksum) {
		fprintf(stderr, "invalid area: [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);
		abort();
	}
}

/* Function prototypes */

struct memarea *ma_new(pm_type_t type, uint32_t size);

/* sbrk memory area procedures */
bool ma_shrink(memarea_t *area, uint32_t pages);
bool ma_expand(memarea_t *area, uint32_t pages);

/* mmap memory area procedures */
void ma_insert(memarea_t *area, memmgr_t *mm);
void ma_split(memarea_t *area, uint32_t offset, uint32_t pages);

/* memory manager procedures */
void mm_init(struct memmgr *mm);
void *mm_alloc(struct memmgr *mm, uint32_t size);
void mm_free(struct memmgr *mm, void *memory);
void mm_print(struct memmgr *mm);

#endif
