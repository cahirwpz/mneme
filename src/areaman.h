#ifndef __AREAMGR_H
#define __AREAMGR_H

#include "common.h"
#include "sysmem.h"
#include <stdio.h>
#include <pthread.h>

/* Memory area structure */

struct area				/* size of this structure will be aligned to 8 bytes boundary */
{
	uint16_t checksum;
	uint16_t flags;	
	uint32_t size;

	struct area *pred;	/* global list predecessor */
	struct area *succ;	/* global list successor */

	struct area *prev;	/* size-bucket list previous area */
	struct area *next;	/* size-bucket list next area */
} __attribute__((aligned(8)));

typedef struct area area_t;

typedef enum { AREA_COALESCE_FAILED, AREA_COALESCE_LEFT, AREA_COALESCE_RIGHT } area_coalesce_t;

#define AREA_FLAG_USED		1
#define AREA_FLAG_MMAP		2
#define AREA_FLAG_SBRK		4
#define AREA_FLAG_SBRK_TOP	8
#define AREA_FLAG_SHM		16
#define AREA_FLAG_READY		32
#define AREA_FLAG_GUARD		64
#define AREA_FLAG_LSTGUARD	128

/* */

struct arealst
{
	struct area;

	uint32_t areacnt;
};

typedef struct arealst arealst_t;

/* */

#define AREAMGR_LIST_COUNT 64

struct areamgr
{
	arealst_t list[AREAMGR_LIST_COUNT];

	uint32_t pagecnt;

	area_t   *guard;

	pthread_mutex_t		freelst_lock;
	pthread_mutexattr_t freelst_lock_attr;

	pthread_mutex_t		lock;
	pthread_mutexattr_t lock_attr;
} __attribute__((aligned(L2_LINE_SIZE)));

typedef struct areamgr areamgr_t;

/* Few inlines to make code more readable :) */

static inline bool area_is_used(area_t *area) {
	return (area->flags & AREA_FLAG_USED);
}

static inline bool area_is_sbrk(area_t *area) {
	return (area->flags & AREA_FLAG_SBRK);
}

static inline bool area_is_mmap(area_t *area) {
	return (area->flags & AREA_FLAG_MMAP);
}

static inline bool area_is_shm(area_t *area) {
	return (area->flags & AREA_FLAG_SHM);
}

static inline bool area_is_ready(area_t *area) {
	return (area->flags & AREA_FLAG_READY);
}

static inline bool area_is_guard(area_t *area) {
	return (area->flags & AREA_FLAG_GUARD);
}

/* Checksum functions for memory area structure */

static inline uint16_t area_checksum(area_t *area)
{
	int bytes = sizeof(area_t) - sizeof(uint16_t);

	return (((uint32_t)area) >> 16) ^ (((uint32_t)area) & 0xFFFF) ^ checksum((uint16_t *)&area->flags, bytes >> 1);
}

static inline void area_touch(area_t *area)
{
	area->checksum = area_checksum(area);
}

static inline void area_valid(area_t *area)
{
	if (area_checksum(area) != area->checksum) {
		fprintf(stderr, "invalid area: [$%.8x; %u; $%.2x]\n", (uint32_t)area, area->size, area->flags);
		abort();
	}
}

/* */

static inline area_t *area_footer(void *begining, uint32_t pages)
{
	return (area_t *)(begining + pages * PAGE_SIZE - sizeof(area_t));
}

static inline void *area_begining(area_t *area)
{
	return (void *)area + sizeof(area_t) - area->size;
}

static inline void areamgr_lock(areamgr_t *areamgr)	  { pthread_mutex_lock(&areamgr->lock); }
static inline void areamgr_unlock(areamgr_t *areamgr) { pthread_mutex_unlock(&areamgr->lock); }

static inline void areamgr_freelst_lock(areamgr_t *areamgr)	  { pthread_mutex_lock(&areamgr->freelst_lock); }
static inline void areamgr_freelst_unlock(areamgr_t *areamgr) { pthread_mutex_unlock(&areamgr->freelst_lock); }

/* Contructor and destructor for memory area */
area_t *area_new(pm_type_t type, uint32_t pages);
bool area_delete(area_t *area);

/* Operations on used-areas */
area_t *area_join(area_t *first, area_t *second);
area_t *area_split(area_t **to_split, uint32_t pages);

/* Memory manager procedures */
areamgr_t *areamgr_init(area_t *area);
area_t *areamgr_alloc_area(areamgr_t *areamgr, uint32_t pages, area_t *addr);
void areamgr_free_area(areamgr_t *areamgr, area_t *area);

void areamgr_add_area(areamgr_t *areamgr, area_t *newarea);
void areamgr_remove_area(areamgr_t *areamgr, area_t *area);

area_t *areamgr_coalesce_area(areamgr_t *areamgr, area_t *area);

#endif
