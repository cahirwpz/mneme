#ifndef __AREAMGR_H
#define __AREAMGR_H

#include "common.h"
#include "sysmem.h"
#include <stdio.h>
#include <pthread.h>

/* === Memory area structure definition ==================================== */

struct area				/* size of this structure will be aligned to 8 bytes boundary */
{
	uint16_t checksum;

	/* flags */
	union {
		struct {
			uint8_t	used:1;
			uint8_t ready:1;
			uint8_t guard:1;
			uint8_t global_guard:1;
			uint8_t	type:2; 
			uint8_t manager:2;
		};
		uint8_t flags0;
	};

	/* cpu which allocated this area */
	uint8_t	 cpu;

	uint32_t size;

	struct {
		/* uint16_t checksum; */
		struct area *prev;	/* previous area on global list */
		struct area *next;	/* next area on global list */
	} global;

	struct {
		/* uint16_t checksum; */
		struct area *prev;	/* previous area on size bucket list */
		struct area *next;	/* next area on size bucket list */
	} local;
} __attribute__((aligned(8)));

typedef struct area area_t;

/* Area flags definition */

#define AREA_TYPE_MMAP			0
#define AREA_TYPE_SBRK			1
#define AREA_TYPE_SBRK_TOP		2
#define AREA_TYPE_SHM			3

#define AREA_MGR_UNMANAGED		0

/* Useful inlines for querying flags status */

static inline bool area_is_used(area_t *area)			{ return area->used; }
static inline bool area_is_ready(area_t *area)			{ return area->ready; }
static inline bool area_is_guard(area_t *area)			{ return area->guard; }
static inline bool area_is_global_guard(area_t *area)	{ return area->global_guard; }

static inline bool area_is_sbrk(area_t *area)	{ return (area->type == AREA_TYPE_SBRK); }
static inline bool area_is_mmap(area_t *area)	{ return (area->type == AREA_TYPE_MMAP); }
static inline bool area_is_shm(area_t *area)	{ return (area->type == AREA_TYPE_SHM); }

/* Checksum functions for memory area structure */

static inline uint16_t area_checksum(area_t *area)/*{{{*/
{
	uint32_t bytes = offsetof(area_t, global) - sizeof(uint16_t);

	return (uint16_t)(((uint32_t)area) >> 16) ^
		   (uint16_t)(((uint32_t)area) & 0xFFFF) ^
		   (uint16_t)checksum((uint16_t *)&area->checksum + 1, bytes >> 1);
}/*}}}*/

static inline void area_touch(area_t *area)/*{{{*/
{
	area->checksum = area_checksum(area);
}/*}}}*/

static inline void area_valid(area_t *area)/*{{{*/
{
	if (area_checksum(area) != area->checksum) {
		fprintf(stderr, "invalid area: [$%.8x; %u; $%.2x] [calc:$%.4x != orig:$%.4x]\n",
				(uint32_t)area, area->size, area->flags0, area_checksum(area), area->checksum);
		hexdump(area, sizeof(area_t));
		abort();
	}
}/*}}}*/

/* Address calculation procedures */

static inline area_t *area_footer(void *begining, uint32_t pages)/*{{{*/
{
	return (area_t *)(begining + pages * PAGE_SIZE - sizeof(area_t));
}/*}}}*/

static inline void *area_begining(area_t *area)/*{{{*/
{
	return (void *)area + sizeof(area_t) - area->size;
}/*}}}*/

static inline void *area_end(area_t *area)/*{{{*/
{
	return (void *)area + sizeof(area_t);
}/*}}}*/

/* Contructor and destructor for memory area */
area_t *area_new(pm_type_t type, uint32_t pages);
bool area_delete(area_t *area);

/* === Memory areas' list structure ======================================== */

struct arealst
{
	struct area;

	uint32_t areacnt;

	pthread_rwlock_t	 lock;
	pthread_rwlockattr_t lock_attr;
};

typedef struct arealst arealst_t;

void arealst_init(arealst_t *arealst);
void arealst_global_add_area(arealst_t *arealst, area_t *newarea, locking_t locking);
void arealst_global_remove_area(arealst_t *arealst, area_t *area, locking_t locking);

bool arealst_has_area(arealst_t *arealst, area_t *addr, locking_t locking);

area_t *arealst_find_area_by_addr(arealst_t *arealst, void *addr, locking_t locking);
area_t *arealst_find_area_by_size(arealst_t *arealst, uint32_t size, locking_t locking);

void arealst_insert_area(arealst_t *arealst, area_t *after, area_t *newarea, locking_t locking);
void arealst_insert_area_by_addr(arealst_t *arealst, area_t *newarea, locking_t locking);
void arealst_insert_area_by_size(arealst_t *arealst, area_t *newarea, locking_t locking);
void arealst_remove_area(arealst_t *arealst, area_t *area, locking_t locking);

area_t *arealst_join_area(arealst_t *global, area_t *first, area_t *second, locking_t locking);
void arealst_split_area(arealst_t *global, area_t **splitted, area_t **remainder, uint32_t pages, locking_t locking);

/* Locking inlines */

static inline void arealst_rdlock(arealst_t *arealst) { pthread_rwlock_rdlock(&arealst->lock); }
static inline void arealst_wrlock(arealst_t *arealst) { pthread_rwlock_wrlock(&arealst->lock); }
static inline void arealst_unlock(arealst_t *arealst) { pthread_rwlock_unlock(&arealst->lock); }

/* === Memory areas' manager structure ===================================== */

#define AREAMGR_LIST_COUNT 64

struct areamgr
{
	arealst_t	global;
	arealst_t	list[AREAMGR_LIST_COUNT];

	/* all pages counter */
	uint32_t	pagecnt;
	/* free pages counter */
	uint32_t	freecnt;
} __attribute__((aligned(L2_LINE_SIZE)));

typedef struct areamgr areamgr_t;

/* Memory manager procedures */
areamgr_t *areamgr_init(area_t *area);
area_t *areamgr_alloc_area(areamgr_t *areamgr, uint32_t pages);
area_t *areamgr_alloc_adjacent_area(areamgr_t *areamgr, area_t *addr, uint32_t pages, direction_t side);
void areamgr_free_area(areamgr_t *areamgr, area_t *area);
bool areamgr_prealloc_area(areamgr_t *areamgr, uint32_t pages);

void areamgr_add_area(areamgr_t *areamgr, area_t *newarea);
void areamgr_remove_area(areamgr_t *areamgr, area_t *area);

area_t *areamgr_coalesce_area(areamgr_t *areamgr, area_t *area);

bool areamgr_expand_area(areamgr_t *areamgr, area_t **area, uint32_t pages, direction_t side);
void areamgr_shrink_area(areamgr_t *areamgr, area_t **area, uint32_t pages, direction_t side);

#endif
