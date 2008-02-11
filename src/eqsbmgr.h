#ifndef __EQSBMGR_H
#define __EQSBMGR_H

#include "common.h"
#include "areamgr.h"

/* Super-block structure */

#define SB_SIZE				1024
#define SB_MAX_AREA_SIZE	32764

struct sb
{
	/* superblock's control field */
	uint16_t 	checksum;
	uint8_t		fblkcnt:7;
	uint8_t		size:7;
	uint8_t		blksize:2;

	/* superblocks's list pointers */
	int16_t	prev;
	int16_t	next;

	/* superblock's bitmap and data */
	union {
		struct {
			uint32_t bitmap[4];
			uint32_t data[0];
		} blk8;		/* 3 used, max. 125 free */
		struct {
			uint32_t bitmap[2];
			uint32_t data[0];
		} blk16;	/* 1 used, max. 63 free */
		struct {
			uint32_t bitmap[2];
			uint32_t data[0];
		} blk24;	/* 1 used, max. 42 free */
		struct {
			uint32_t bitmap[1];
			uint32_t padding[5];
			uint32_t data[0];
		} blk32;	/* 1 used, max. 31 free */
		uint32_t bitmap[0];
	};
};

typedef struct sb sb_t;

/* Super-blocks list */

struct sb_list
{
	/* list header */
	sb_t *first;
	sb_t *last;

	/* list guard fields */
	uint32_t sbcnt;

	/* list guard block */
	pthread_rwlock_t	 lock;
	pthread_rwlockattr_t lock_attr;
};

typedef struct sb_list sb_list_t;

/* Manager structure */

struct eqsbmgr
{
	arealst_t arealst;

	sb_list_t nonempty[4];		/* 0: 8B; 1: 16B; 2: 24B; 3: 32B */
	sb_list_t groups[4];		/* 0: 1 SBs; 1: 2 SBs; 2: 3SBs; 3: 4SBs */

	areamgr_t *areamgr;
};

typedef struct eqsbmgr eqsbmgr_t;

/* function prototypes */
void eqsbmgr_init(eqsbmgr_t *eqsbmgr, areamgr_t *areamgr);
void *eqsbmgr_alloc(eqsbmgr_t *eqsbmgr, uint32_t size, uint32_t alignment);
bool eqsbmgr_realloc(eqsbmgr_t *eqsbmgr, void *memory, uint32_t new_size);
bool eqsbmgr_free(eqsbmgr_t *eqsbmgr, void *memory);
void eqsbmgr_print(eqsbmgr_t *eqsbmgr);

#endif
