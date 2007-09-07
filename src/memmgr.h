#ifndef __MEMMGR_H
#define __MEMMGR_H

#include "common.h"
#include "sysmem.h"
#include <stdio.h>

/* Memory area structure */

struct memarea
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;
	uint32_t used;

	struct memarea *prev;
	struct memarea *next;

	struct memblock *free;
};

typedef struct memarea memarea_t;

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
void ma_print(struct memarea *area);

struct memarea *mm_init(void);
void *mm_alloc(struct memarea *mm, uint32_t size);
void mm_free(struct memarea *mm, void *memory);
void mm_print(struct memarea *mm);

#endif
