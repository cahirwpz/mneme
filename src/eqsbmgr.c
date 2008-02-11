/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	
 */

#include "eqsbmgr.h"

#include <strings.h>
#include <string.h>

/* Declaration of superblocks' list */

static inline sb_t *sb_get_prev(sb_t *self)
	{ return (void *)self + self->prev * SB_SIZE; }

static inline void sb_set_prev(sb_t *self, sb_t *prev)
	{ self->prev = ((int32_t)prev / SB_SIZE) - ((int32_t)self / SB_SIZE); }

static inline sb_t *sb_get_next(sb_t *self)
	{ return (void *)self + self->next * SB_SIZE; }

static inline void sb_set_next(sb_t *self, sb_t *next)
	{ self->next = ((int32_t)next / SB_SIZE) - ((int32_t)self / SB_SIZE); }

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
 *
 */

static uint8_t sb_get_blocks(sb_t *sb)/*{{{*/
{
	int32_t blocks  = (sb->size << 3);

	if (sb->blksize == 0)
		blocks -= offsetof(sb_t, blk8.data);
	else if (sb->blksize == 1)
		blocks -= offsetof(sb_t, blk16.data);
	else if (sb->blksize == 2)
		blocks -= offsetof(sb_t, blk24.data);
	else
		blocks -= offsetof(sb_t, blk32.data);

	return (blocks >> 3) / (sb->blksize + 1);
}/*}}}*/

/**
 *
 */

static void sb_init(sb_t *sb, uint8_t blksize, uint32_t size)/*{{{*/
{
	assert(blksize < 4);
	assert(!(((uint32_t)sb + sizeof(sb_t) - size) & (SB_SIZE - 1)));

	sb->blksize = blksize;
	sb->size    = size >> 3;

	/* initialize bitmap */
	uint8_t *bitmap = (uint8_t *)sb->bitmap;
	int32_t blocks  = sb_get_blocks(sb);

	int32_t i;

	for (i = 0; blocks > 7; blocks -= 8, i++)
		bitmap[i] = 0xFF;

	if (blocks > 0) {
		bitmap[i] = 0x00;

		while (blocks > 0) {
			sb->bitmap[i] |= (1 << (8 - blocks));
			blocks--;
		}
	}

	sb->fblkcnt = blocks;
}/*}}}*/

/**
 *
 */

static int16_t sb_alloc(sb_t *sb)/*{{{*/
{
	if (sb->fblkcnt == 0)
		return -1;

	uint32_t i = 0, j = 0, lastblk = sb_get_blocks(sb);

	while ((j == 0) && (i < lastblk)) {
		j = ffs(sb->bitmap[i >> 5]);
		i += 32;
	}

	assert(j > 0);

	sb->fblkcnt--;
	sb->bitmap[i >> 5] |= (1 << --j);

	return (i + j);
}/*}}}*/

/**
 *
 */

static uint16_t sb_alloc_indexed(sb_t *sb, uint32_t index)/*{{{*/
{
	if (sb->fblkcnt == 0)
		return -1;

	uint32_t i = index >> 5, j = index & 0x1F, lastblk = sb_get_blocks(sb);

	assert(index < lastblk);

	if (sb->bitmap[i] & (1 << j)) {
		sb->fblkcnt--;

		return index;
	}

	return -1;
}/*}}}*/

/**
 *
 */

static void sb_free(sb_t *sb, uint16_t index)/*{{{*/
{
	uint32_t i = index >> 5, j = index & 0x1F, lastblk = sb_get_blocks(sb);

	assert(index < lastblk);
	assert(sb->bitmap[i] & (1 << j));

	sb->fblkcnt++;
	sb->bitmap[i] &= ~(1 << j);
}/*}}}*/

/**
 *
 */

void eqsblkmgr_init(eqsbmgr_t *eqsbmgr, areamgr_t *areamgr)/*{{{*/
{
	arealst_init(&eqsbmgr->arealst);

	eqsbmgr->areamgr = areamgr;

	uint32_t i;

	for (i = 0; i < 4; i++) {
		sb_list_init(&eqsbmgr->nonempty[i]);
		sb_list_init(&eqsbmgr->groups[i]);
	}
}/*}}}*/

/**
 *
 */

void *eqsbmgr_alloc(eqsbmgr_t *eqsbmgr, uint32_t size, uint32_t alignment)/*{{{*/
{
}/*}}}*/

/**
 *
 */

bool eqsbmgr_realloc(eqsbmgr_t *eqsbmgr, void *memory, uint32_t new_size)/*{{{*/
{
}/*}}}*/

/**
 *
 */

bool eqsbmgr_free(eqsbmgr_t *eqsbmgr, void *memory)/*{{{*/
{
}/*}}}*/

/**
 *
 */

void eqsbmgr_print(eqsbmgr_t *eqsbmgr)/*{{{*/
{
}/*}}}*/

