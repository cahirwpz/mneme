#ifndef __MEMMGR_H
#define __MEMMGR_H

#include "common.h"
#include "sysmem.h"
#include <stdio.h>

/* Memory area structure */

struct memarea				/* size of this structure will be aligned to 16 bytes boundary */
{
	uint16_t checksum;		// 2
	uint16_t flags;			// 4
	uint32_t size;			// 8
	uint32_t used;			// 12

	struct memarea *prev;	// 16
	struct memarea *next;	// 20

	struct memblock *free;	// 24
	struct memblock *last;	// 28
};

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
void ma_print(struct memarea *area);

void mm_init(struct memmgr *mm);
void *mm_alloc(struct memmgr *mm, uint32_t size);
void mm_free(struct memmgr *mm, void *memory);
void mm_print(struct memmgr *mm);

#endif
