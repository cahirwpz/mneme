/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	
 */

#include "eqsbmgr.h"
#include "areamgr.h"

#include <strings.h>
#include <string.h>

#define SB_SIZE				1024
#define SB_MAX_AREA_SIZE	32764

/* Super-block structure */

struct sb
{
	/* superblock's control field */
	uint16_t 	checksum;
	uint8_t		fblkcnt:7;				/* if fblkcnt == 127 then block is free */
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

/* Superblocks' manager structure */

struct sb_mgr
{
	sb_list_t nonempty[4];		/* 0: 8B; 1: 16B; 2: 24B; 3: 32B */
	sb_list_t groups[4];		/* 0: 1 SBs; 1: 2 SBs; 2: 3SBs; 3: 4SBs */

	uint16_t free;
	uint16_t all;
};

typedef struct sb_mgr sb_mgr_t;

/* Declaration of superblocks' list */

static inline sb_t *sb_get_prev(sb_t *self)/*{{{*/
{
	return (void *)self + self->prev * SB_SIZE;
}/*}}}*/

static inline void sb_set_prev(sb_t *self, sb_t *prev)/*{{{*/
{
	self->prev = ((int32_t)prev / SB_SIZE) - ((int32_t)self / SB_SIZE);
}/*}}}*/

static inline sb_t *sb_get_next(sb_t *self)/*{{{*/
{
	return (void *)self + self->next * SB_SIZE;
}/*}}}*/

static inline void sb_set_next(sb_t *self, sb_t *next)/*{{{*/
{
	self->next = ((int32_t)next / SB_SIZE) - ((int32_t)self / SB_SIZE);
}/*}}}*/

#define __LIST				sb_list
#define __LIST_T			sb_list_t
#define __KEY_T				sb_t *
#define __ITEM_T			sb_t
#define __COUNTER(list)		list->sbcnt
#define __FIRST(list)		list->first
#define __LAST(list)		list->last
#define __PREV(obj)			sb_get_prev(obj)
#define __NEXT(obj)			sb_get_next(obj)
#define __SET_PREV(obj,ptr)	sb_set_prev(obj, ptr)
#define __SET_NEXT(obj,ptr)	sb_set_next(obj, ptr)
#define __KEY(obj)			obj
#define __LOCK(obj)			obj->lock
#define __LOCK_ATTR(obj)	obj->lock_attr
#define __KEY_LT(a,b)		(a) < (b)
#define __KEY_EQ(a,b)		(a) == (b)

#include "common-list0.c"

/**
 * Calculate how many blocks are managed by given superblock.
 * 
 * @param self
 * @return
 */

static uint8_t sb_get_blocks(sb_t *self)/*{{{*/
{
	int32_t blocks  = (self->size << 3);

	if (self->blksize == 0)
		blocks -= offsetof(sb_t, blk8.data);
	else if (self->blksize == 1)
		blocks -= offsetof(sb_t, blk16.data);
	else if (self->blksize == 2)
		blocks -= offsetof(sb_t, blk24.data);
	else
		blocks -= offsetof(sb_t, blk32.data);

	return (blocks >> 3) / (self->blksize + 1);
}/*}}}*/

/**
 * Calculates superblock position from given address.
 *
 * @param address
 * @return
 */

static inline sb_t *sb_get_from_address(void *address)/*{{{*/
{
	return (sb_t *)((uint32_t)address & 0xFFFFC00);
}/*}}}*/

/**
 *
 */

static inline void *sb_get_data(sb_t *self)/*{{{*/
{
	void *base;

	if (self->blksize == 0)
		base = &self->blk8.data;
	else if (self->blksize == 1)
		base = &self->blk16.data;
	else if (self->blksize == 2)
		base = &self->blk24.data;
	else
		base = &self->blk32.data;
	
	return base;
}/*}}}*/

/**
 * Initialize superblock of given length and blocks' size.
 *
 * @param self
 * @param blksize
 * @param size
 */

static void sb_init(sb_t *self, uint8_t blksize, uint32_t size)/*{{{*/
{
	assert(blksize < 4);
	assert(!(((uint32_t)self + sizeof(sb_t) - size) & (SB_SIZE - 1)));

	self->blksize = blksize;
	self->size    = size >> 3;

	/* initialize bitmap */
	uint8_t *bitmap = (uint8_t *)self->bitmap;
	int32_t blocks  = sb_get_blocks(self);

	int32_t i;

	for (i = 0; blocks > 7; blocks -= 8, i++)
		bitmap[i] = 0xFF;

	if (blocks > 0) {
		bitmap[i] = 0x00;

		while (blocks > 0) {
			self->bitmap[i] |= (1 << (8 - blocks));
			blocks--;
		}
	}

	self->fblkcnt = blocks;
}/*}}}*/

/**
 * Allocate a block within given superblock and return its' index.
 *
 * @param self
 * @return
 */

static int16_t sb_alloc(sb_t *self)/*{{{*/
{
	if (self->fblkcnt == 0)
		return -1;

	uint32_t i = 0, j = 0, lastblk = sb_get_blocks(self);

	while ((j == 0) && (i < lastblk)) {
		j = ffs(self->bitmap[i >> 5]);
		i += 32;
	}

	assert(j > 0);

	self->fblkcnt--;
	self->bitmap[i >> 5] |= (1 << --j);

	return (i + j);
}/*}}}*/

/**
 * Allocate a block of given index within a superblock.
 *
 * @param self
 * @param index
 * @return 
 */

static uint16_t sb_alloc_indexed(sb_t *self, uint32_t index)/*{{{*/
{
	if (self->fblkcnt == 0)
		return -1;

	uint32_t i = index >> 5, j = index & 0x1F, lastblk = sb_get_blocks(self);

	assert(index < lastblk);

	if (self->bitmap[i] & (1 << j)) {
		self->fblkcnt--;

		return index;
	}

	return -1;
}/*}}}*/

/**
 * Free a block that has given index.
 *
 * @param self
 * @param index
 */

static void sb_free(sb_t *self, uint16_t index)/*{{{*/
{
	uint32_t i = index >> 5, j = index & 0x1F, lastblk = sb_get_blocks(self);

	assert(index < lastblk);
	assert(self->bitmap[i] & (1 << j));

	self->fblkcnt++;
	self->bitmap[i] &= ~(1 << j);
}/*}}}*/

/**
 * Get i-th superblock within a group of superblocks in one memory page.
 *
 * @param self
 * @param i
 * @return
 */

static inline sb_t *sb_grp_nth(sb_t *self, uint16_t i) {/*{{{*/
	return (sb_t *)(((uint32_t)self & 0xFFFFF000) + i * SB_SIZE);
}/*}}}*/

/**
 * Get index of superblock inside a memory page.
 *
 * @param self	superblock address
 * @return 		{ i: 0 <= i < 4 }
 */

static inline uint16_t sb_grp_index(sb_t *self) {/*{{{*/
	return ((uint32_t)self - ((uint32_t)self & 0xFFFFF000)) / SB_SIZE;
}/*}}}*/

/**
 * Calculate address of superblocks' manager inside given area.
 *
 * @param self
 * @return
 */

static inline sb_mgr_t *sb_mgr_from_area(area_t *self) {/*{{{*/
	return (sb_mgr_t *)((uint32_t)self - sizeof(sb_mgr_t));
}/*}}}*/

/**
 * Initialize superblocks' manager.
 *
 * @param self
 */

static void sb_mgr_init(sb_mgr_t *self)/*{{{*/
{
	uint32_t i;

	for (i = 0; i < 4; i++) {
		sb_list_init(&self->nonempty[i]);
		sb_list_init(&self->groups[i]);
	}

	self->free = 0;
	self->all  = 0;
}/*}}}*/

/**
 * Allocate a new superblock from superblocks' manager.
 *
 * @param self
 * @return
 */

static sb_t *sb_mgr_alloc(sb_mgr_t *self)/*{{{*/
{
	sb_t *sb = NULL;

	if (self->free > 0) {
		int32_t i = 0;

		while ((i < 4) && (self->groups[i].first))
			i++;

		if (i < 4) {
			sb_t *shorter;

			sb      = sb_list_pop(&self->groups[i], DONTLOCK);
			shorter = (sb_t *)((void *)sb + SB_SIZE);

			if (sb->blksize > 0) {
				shorter->blksize = sb->blksize - 1;

				sb_list_push(&self->groups[sb->blksize], shorter, DONTLOCK);
			}

			sb->fblkcnt = sb_get_blocks(sb);

			self->free--;
		}
	}

	return sb;
}/*}}}*/

/**
 * Return a superblock to superblocks' manager.
 *
 * @param self
 * @param sb
 */

static void sb_mgr_free(sb_mgr_t *self, sb_t *sb)/*{{{*/
{
	int32_t i = sb_grp_index(sb);

	if (i < 3) {
		sb_t *next = sb_grp_nth(sb, i + 1);

		if (next->fblkcnt == 127) {
			sb->blksize = next->blksize + 1;

			sb_list_remove(&self->groups[next->blksize], next, DONTLOCK);
		}
	}

	while (i > 0) {
		sb_t *prev = sb_grp_nth(sb, i - 1);

		if (prev->fblkcnt != 127) {
			sb_t *curr = sb_grp_nth(sb, i);

			if (curr != sb) {
				sb_list_remove(&self->groups[curr->blksize], curr, DONTLOCK);

				curr->blksize += sb->blksize + 1;

				sb = curr;
			}
	
			break;
		}

		i--;
	}

	sb->fblkcnt = 127;

	sb_list_push(&self->groups[sb->blksize], sb, DONTLOCK);

	self->free++;
}/*}}}*/

/**
 *
 */

static void sb_mgr_print(sb_mgr_t *self)/*{{{*/
{
}/*}}}*/

/**
 *
 */

void eqsblkmgr_init(eqsbmgr_t *self, areamgr_t *areamgr)/*{{{*/
{
	arealst_init(&self->arealst);

	self->areamgr = areamgr;
}/*}}}*/

/**
 *
 */

void *eqsbmgr_alloc(eqsbmgr_t *self, uint32_t size, uint32_t alignment)/*{{{*/
{
	uint8_t blksize = (size - 1) >> 3;

	assert(size < 4);

	/* alignment is not supported due to much more complex implementation */
	assert(alignment == 0);

	sb_t *sb = NULL;

	/* browse list of managed areas */
	area_t *area = (area_t *)self->arealst.local.next;

	while (!area_is_guard(area)) {
		sb_mgr_t *mgr = sb_mgr_from_area(area);

		/* get first superblock from nonempty sbs stack */
		sb = mgr->nonempty[blksize].first;

		/* if no superblock found get one from free superblocks list */
		if (sb == NULL)
			sb = sb_mgr_alloc(mgr);

		if (sb != NULL)
			break;

		area = area->local.next;
	}

	/* ouch... need to get new pages from area manager */
	if (sb == NULL) {
		area_t *newarea = areamgr_alloc_area(self->areamgr, 1);

		arealst_insert_area_by_addr(&self->arealst, (void *)newarea, LOCK);

		/* failed to obtain new pages :( */
		return NULL;
	}

	return sb_get_data(sb) + sb_alloc(sb) * ((blksize + 1) << 3);
}/*}}}*/

/**
 *
 */

bool eqsbmgr_free(eqsbmgr_t *self, void *memory)/*{{{*/
{
	sb_mgr_t *mgr = NULL;

	/* browse list of managed areas */
	area_t *area = (area_t *)self->arealst.local.next;

	while (!area_is_guard(area)) {
		if ((area_begining(area) <= memory) && (memory <= area_end(area))) {
			mgr = sb_mgr_from_area(area);
			break;
		}

		area = area->local.next;
	}

	if (mgr != NULL) {
		sb_t *sb = sb_get_from_address(memory);

		uint8_t i = (uint32_t)(memory - sb_get_data(sb)) / ((sb->blksize + 1) << 3);

		sb_free(sb, i);

		uint8_t blocks = sb_get_blocks(sb);

		if (blocks == sb->fblkcnt)
			sb_mgr_free(mgr, sb);
	}

	return (mgr != NULL);
}/*}}}*/

/**
 *
 */

uint32_t eqsbmgr_purge(eqsbmgr_t *self, bool aggressive)/*{{{*/
{
	return 0;
}/*}}}*/

/**
 * Resize allocated block. In fact it's only checked if block will fit
 * in exactly the same place without losing memory.
 */

bool eqsbmgr_realloc(eqsbmgr_t *self, void *memory, uint32_t new_size)/*{{{*/
{
	assert(new_size <= 32);

	uint8_t  new_blksize = (new_size - 1) >> 3;
	sb_mgr_t *mgr = NULL;

	/* browse list of managed areas */
	area_t *area = (area_t *)self->arealst.local.next;

	while (!area_is_guard(area)) {
		if ((area_begining(area) <= memory) && (memory <= area_end(area))) {
			mgr = sb_mgr_from_area(area);
			break;
		}

		area = area->local.next;
	}

	assert(mgr != NULL);

	/* check if there is need to resize block */
	sb_t *sb = sb_get_from_address(memory);

	return (sb->blksize == new_blksize);
}/*}}}*/

/**
 *
 */

void eqsbmgr_print(eqsbmgr_t *self)/*{{{*/
{
	arealst_rdlock(&self->arealst);

	area_t *area = (area_t *)&self->arealst;

	fprintf(stderr, "\033[1;36m blkmgr at $%.8x [%d areas]:\033[0m\n",
			(uint32_t)self, self->arealst.areacnt);

	bool error = FALSE;
	uint32_t areacnt = 0;

	while (TRUE) {
		area_valid(area);

		if (!area_is_guard(area)) {
			fprintf(stderr, "\033[1;31m  $%.8x - $%.8x: %8d : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area_begining(area), (uint32_t)area_end(area), area->size,
					(uint32_t)area->local.prev, (uint32_t)area->local.next);

			sb_mgr_print(sb_mgr_from_area(area));
		}
		else
			fprintf(stderr, "\033[1;33m  $%.8x %11s: %8s : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area, "", "guard", (uint32_t)area->local.prev, (uint32_t)area->local.next);

		if (area_is_guard(area->local.next))
			break;

		if (!area_is_guard(area) && (area >= area->local.next))
			error = TRUE;

		area = area->local.next;

		areacnt++;
	}

	assert(!error);

	assert(areacnt == self->arealst.areacnt);

	arealst_unlock(&self->arealst);

}/*}}}*/

