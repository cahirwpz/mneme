#ifndef __AREAMAN_H
#define __AREAMAN_H

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

typedef enum { MA_COALESCE_FAILED, MA_COALESCE_LEFT, MA_COALESCE_RIGHT } ma_coalesce_t;

/* Flags definition */

#define MA_FLAG_READY	1
#define MA_FLAG_MMAP	2
#define MA_FLAG_SBRK	4
#define MA_FLAG_SHM		8
#define MA_FLAG_GUARD	16

/* Few inlines to make code more readable :) */

static inline bool ma_is_guard(memarea_t *area) {
	return (area->flags & MA_FLAG_GUARD);
}

static inline bool ma_is_sbrk(memarea_t *area) {
	return (area->flags & MA_FLAG_SBRK);
}

static inline bool ma_is_mmap(memarea_t *area) {
	return (area->flags & MA_FLAG_MMAP);
}

static inline bool ma_is_ready(memarea_t *area) {
	return (area->flags & MA_FLAG_READY);
}

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
void ma_init_manager(memarea_t *mm);
void ma_add(memarea_t *area, memarea_t *mm);
memarea_t *ma_new(pm_type_t type, uint32_t size);

/* sbrk memory area procedures */
bool ma_shrink_at_end(memarea_t *area, uint32_t pages);
bool ma_shrink_at_beginning(memarea_t **area, uint32_t pages);
bool ma_expand(memarea_t *area, uint32_t pages);
bool ma_remove(memarea_t *area);

/* mmap memory area procedures */
memarea_t *ma_split(memarea_t *area, void *cut, uint32_t pages);
memarea_t *ma_coalesce(memarea_t *area, ma_coalesce_t *direction);

#endif
