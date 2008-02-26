/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	
 */

#include "eqsbmgr.h"
#include "areamgr.h"

#include <strings.h>
#include <string.h>

#define SB_SIZE			1024
#define SB_COUNT_MAX	32764
#define AREA_MAX_SIZE 	SB_COUNT_MAX * SB_SIZE

/* Super-block structure */

struct sb
{
	/* superblock's control field */
	uint16_t 	checksum;
	uint16_t	fblkcnt:7;				/* if fblkcnt == 127 then block is free */
	uint16_t	size:7;
	uint16_t	blksize:2;

	/* superblocks' list pointers */
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

static void sb_mgr_print(sb_mgr_t *self);

/* Declaration of superblocks' list */

static inline sb_t *sb_get_prev(sb_t *self)/*{{{*/
{
	return (self->prev == 0) ? NULL : (void *)self + self->prev * SB_SIZE;
}/*}}}*/

static inline void sb_set_prev(sb_t *self, sb_t *prev)/*{{{*/
{
	self->prev = (prev == NULL) ? 0 : ((int32_t)prev / SB_SIZE) - ((int32_t)self / SB_SIZE);
}/*}}}*/

static inline sb_t *sb_get_next(sb_t *self)/*{{{*/
{
	return (self->next == 0) ? NULL : (void *)self + self->next * SB_SIZE;
}/*}}}*/

static inline void sb_set_next(sb_t *self, sb_t *next)/*{{{*/
{
	self->next = (next == NULL) ? 0 : ((int32_t)next / SB_SIZE) - ((int32_t)self / SB_SIZE);
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
#define __KEY_LT(a,b)		(a) < (b)
#define __KEY_EQ(a,b)		(a) == (b)

#include "common-list1.c"

/**
 * Calculate how many blocks are managed by given superblock.
 * 
 * @param self
 * @return
 */

static uint8_t sb_get_blocks(sb_t *self)/*{{{*/
{
	int32_t blocks  = (self->size + 1) << 3;

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
	return (sb_t *)((uint32_t)address & 0xFFFFFC00);
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
 * Prepare superblock to provide blocks of size <i>blksize</i>.
 *
 * @param self
 * @param blksize
 */

static void sb_prepare(sb_t *self, uint8_t blksize)/*{{{*/
{
	assert(blksize < 4);

	self->blksize = blksize;

	/* initialize bitmap */
	int32_t blocks = sb_get_blocks(self);

	self->fblkcnt = blocks;

	int32_t i;

	for (i = 0; blocks > 31; blocks -= 32, i++)
		self->bitmap[i] = 0xFFFFFFFF;

	if (blocks > 0)
		self->bitmap[i] = ~((1 << (32 - blocks)) - 1);
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

	while (i < lastblk) {
		uint32_t data = self->bitmap[i >> 5];

		j = (data == 0) ? 0 : __builtin_clz(data) + 1;
		
		if (j > 0)
			break;

		i += 32;
	}

	assert(j > 0);

	self->fblkcnt--;
	self->bitmap[i >> 5] &= ~(1 << (32 - j));

	return (i + j - 1);
}/*}}}*/

/**
 * Allocate a block of given index within a superblock.
 *
 * @param self
 * @param index
 * @return 
 */

#if 0
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
#endif

/**
 * Free a block that has given index.
 *
 * @param self
 * @param index
 */

static void sb_free(sb_t *self, uint32_t index)/*{{{*/
{
	DEBUG("Free block of index %u in SB at $%.8x\n", index, (uint32_t)self);

	uint32_t i = index >> 5, j = 31 - (index & 0x1F), lastblk = sb_get_blocks(self);

	assert(index < lastblk);
	assert((self->bitmap[i] & (1 << j)) == 0);

	self->fblkcnt++;
	self->bitmap[i] |= (1 << j);
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
	DEBUG("Initialize SBs' manager at $%.8x.\n", (uint32_t)self);

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

static sb_t *sb_mgr_alloc(sb_mgr_t *self, uint16_t blksize)/*{{{*/
{
	DEBUG("Allocate new SB from SBs' manager at $%.8x.\n", (uint32_t)self);

	sb_t *sb = NULL;

	if (self->free > 0) {
		int32_t i = 0;

		while ((i < 4) && (self->groups[i].first == NULL))
			i++;

		if (i < 4) {
			sb_t *shorter;

			sb      = sb_list_pop(&self->groups[i]);
			shorter = (sb_t *)((void *)sb + SB_SIZE);

			if (sb->blksize > 0) {
				shorter->blksize = sb->blksize - 1;

				sb_list_push(&self->groups[shorter->blksize], shorter);
			}

			sb->fblkcnt = sb_get_blocks(sb);

			self->free--;
		}

		sb_prepare(sb, blksize);
		sb_list_push(&self->nonempty[sb->blksize], sb);
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
	DEBUG("Return SB at $%.8x to SBs' manager at $%.8x.\n", (uint32_t)sb, (uint32_t)self);

	int32_t i = sb_grp_index(sb);

	sb->blksize = 0;
	sb->fblkcnt = 127;

	if (i < 3) {
		sb_t *next = sb_grp_nth(sb, i + 1);

		if (next->fblkcnt == 127) {
			sb->blksize = next->blksize + 1;

			sb_list_remove(&self->groups[next->blksize], next);
		}
	}

	int32_t old_i = i;

	while (i > 0) {
		sb_t *prev = sb_grp_nth(sb, i - 1);

		if (prev->fblkcnt != 127)
			break;

		i--;
	}

	if (i != old_i) {
		sb_t *curr = sb_grp_nth(sb, i);

		sb_list_remove(&self->groups[curr->blksize], curr);

		curr->blksize += sb->blksize + 1;

		sb = curr;
	}

	sb_list_push(&self->groups[sb->blksize], sb);

	self->free++;
}/*}}}*/

/**
 * Add memory pages to superblocks' manager and initialize them.
 */

static void sb_mgr_add(sb_mgr_t *self, void *memory, uint16_t superblocks)/*{{{*/
{
	DEBUG("Add %u SBs starting at $%.8x to SBs' manager at $%.8x.\n",
		  (uint32_t)superblocks, (uint32_t)memory, (uint32_t)self);

	uint16_t i;
	sb_t *sb = NULL;

	for (i = 0; i < superblocks; i++) {
		sb = (sb_t *)((uint32_t)memory + i * SB_SIZE);

		sb->size = (SB_SIZE - 1) >> 3;
		sb->fblkcnt = 127;
		sb->blksize = 3;

		sb->prev = 0;
		sb->next = 0;

		if ((i & 3) == 0)
			sb_list_push(&self->groups[sb->blksize], sb);

		self->free++;
		self->all++;
	}

	/* if added memory overlaps superblocks' manager then shorten last superblock */
	if (sb == sb_get_from_address(self)) {
		sb->size = (((uint32_t)self - (uint32_t)sb) >> 3) - 1;

		sb_t *prev = (sb_t *)((uint32_t)sb_get_from_address(memory) - SB_SIZE);

		/* check if first block before added pages exists */
		if (self->free > superblocks) {
			int32_t old_blocks  = sb_get_blocks(prev);

			prev->size = 127;

			int32_t blocks = sb_get_blocks(prev);

			if (prev->fblkcnt < 127) {
				while (blocks >= old_blocks)
					sb_free(prev, --blocks);

				if (old_blocks == 0)
					sb_list_push(&self->nonempty[prev->blksize], prev);
			}
		}
	}
}/*}}}*/

/**
 * Print all superblocks in this SBs' manager.
 */

static void sb_mgr_print(sb_mgr_t *self)/*{{{*/
{
	fprintf(stderr, "\033[1;34m   sbmgr at $%.8x [all: %u; free: %u]\033[0m\n",
			(uint32_t)self, self->all, self->free);

	sb_t *base = (sb_t *)((uint32_t)sb_get_from_address(self) - (uint32_t)(self->all - 1) * SB_SIZE);

	uint32_t i, j;

	for (i = 0; i < self->all; i++) {
		sb_t *sb = (sb_t *)((uint32_t)base + SB_SIZE * i);

		if (sb->fblkcnt == 127) {
			fprintf(stderr, "\033[1;32m   $%.8x: %4d\033[0m\n",
					(uint32_t)sb, (sb->size + 1) << 3);
		} else {
			fprintf(stderr, "\033[1;31m   $%.8x: %4d : %4d : %4d : ",
					(uint32_t)sb, (sb->size + 1) << 3, (sb->blksize + 1) << 3, sb->fblkcnt);

			uint32_t *data = (uint32_t *)sb->bitmap;

			for (j = 0; j < sb_get_blocks(sb); j += 32)
				fprintf(stderr, "%.8x ", data[j >> 5]);

			fprintf(stderr, "\033[0m\n");
		}
	}
	
	{
		fprintf(stderr, "\033[0;35m   nonempty : ");

		for (i = 0; i < 4; i++)
			fprintf(stderr, "($%.8x:$%.8x:%u)", (uint32_t)self->nonempty[i].first,
					(uint32_t)self->nonempty[i].last, self->nonempty[i].sbcnt);

		fprintf(stderr, "\033[0m\n");
	}

	{
		fprintf(stderr, "\033[0;35m   groups   : ");

		for (i = 0; i < 4; i++)
			fprintf(stderr, "($%.8x:$%.8x:%u)", (uint32_t)self->groups[i].first,
					(uint32_t)self->groups[i].last, self->groups[i].sbcnt);

		fprintf(stderr, "\033[0m\n");
	}
}/*}}}*/

/**
 *
 */

sb_mgr_t *sb_mgr_expand(sb_mgr_t *mgr, uint32_t newsbs, direction_t side)
{
	DEBUG("Will expand SB's manager at $%.8x by %u SBs from %s side.\n",
		  (uint32_t)mgr, newsbs, (side == LEFT) ? "left" : "right");

	assert(newsbs > 0);
	assert(mgr->all <= SB_COUNT_MAX - newsbs);

	sb_mgr_t *oldmgr = mgr;
	sb_t     *oldsb  = sb_get_from_address(mgr);

	if (side == LEFT) {
		sb_mgr_add(mgr, (void *)((uint32_t)oldsb - (newsbs + mgr->all - 1) * SB_SIZE), newsbs);
	} else {
		/* copy old superblocks' manager to new location */
		mgr = (sb_mgr_t *)((uint32_t)mgr + newsbs * SB_SIZE);

		memcpy(mgr, oldmgr, sizeof(sb_mgr_t));

		DEBUG("Moved SB's manager to $%.8x [%u/%u].\n", (uint32_t)mgr, mgr->free, mgr->all);

		/* add newly allocated pages to superblocks' manager */
		sb_mgr_add(mgr, (void *)((uint32_t)oldsb + SB_SIZE), newsbs);

		/* free some unused blocks */
		uint32_t old_blocks = sb_get_blocks(oldsb);

		oldsb->size = (SB_SIZE >> 3) - 1;

		uint32_t blocks = sb_get_blocks(oldsb);

		DEBUG("Freeing %u unused blocks in SB at $%.8x\n", blocks - old_blocks, (uint32_t)oldsb);

		uint32_t freeblocks = oldsb->fblkcnt;

		while (blocks > old_blocks)
			sb_free(oldsb, --blocks);

		if (freeblocks == 0)
			sb_list_push(&mgr->nonempty[oldsb->blksize], oldsb);
	}

	return mgr;
}

/**
 * Initialize equally-sized blocks' manager.
 *
 * @param self		equally-sized blocks' manager structure
 * @param areamgr	address of global area manager
 */

void eqsbmgr_init(eqsbmgr_t *self, areamgr_t *areamgr)/*{{{*/
{
	arealst_init(&self->arealst);

	self->areamgr = areamgr;
}/*}}}*/

/**
 * Allocate a block from equally-sized blocks' manager.
 * Manager does not support alignment!
 *
 * @param self		equally-sized blocks' manager structure
 * @param size		{i: i \in [1; 32] }
 * @param alignment {i: i = 2^k, k \in [0, 3] }
 * @return			address of allocated block
 */

void *eqsbmgr_alloc(eqsbmgr_t *self, uint32_t size, uint32_t alignment)/*{{{*/
{
	if (alignment) {
		DEBUG("\033[37;1mRequested block of size %u aligned to %u bytes boundary.\033[0m\n", size, alignment);
	} else {
		DEBUG("\033[37;1mRequested block of size %u.\033[0m\n", size);
	}

	uint8_t blksize = (size - 1) >> 3;

	assert(blksize < 4);

	/* alignment is not supported due to much more complex implementation */
	assert(alignment <= 8);

	sb_t     *sb   = NULL;
    sb_mgr_t *mgr  = NULL;
	area_t   *area = NULL;

	arealst_wrlock(&self->arealst);

	{
		DEBUG("Try to find superblock with free blocks.\n");

		area = (area_t *)self->arealst.local.next;

		while (!area_is_guard(area)) {
			mgr = sb_mgr_from_area(area);

			/* get first superblock from nonempty sbs stack */
			if ((sb = mgr->nonempty[blksize].first))
				break;

			area = area->local.next;
		}
	}

	if (sb == NULL) {
		DEBUG("Try to allocate unused superblock.\n");

		area = (area_t *)self->arealst.local.next;

		while (!area_is_guard(area)) {
			mgr = sb_mgr_from_area(area);

			/* try to allocate superblock */
			if ((sb = sb_mgr_alloc(mgr, blksize)))
				break;

			area = area->local.next;
		}
	}

	/* last attempt: ouch... need to get new pages from area manager */
	if (sb == NULL) {
		DEBUG("No free blocks and superblocks found!\n");
		
		/* first attempt: try adjacent areas */
		areamgr_prealloc_area(self->areamgr, 1);

		area = (area_t *)self->arealst.local.next;

		DEBUG("Try to merge adjacent pages to one of superblocks' manager.\n");

		while (!area_is_guard(area)) {
			mgr = sb_mgr_from_area(area);

			if (mgr->all * SB_SIZE < AREA_MAX_SIZE) {
				uint32_t oldsize = area->size;
				direction_t side = NONE;

				if (areamgr_expand_area(self->areamgr, &area, 1, LEFT))
					side = LEFT;
				else if (areamgr_expand_area(self->areamgr, &area, 1, RIGHT))
					side = RIGHT;

				if (side != NONE) {
					uint32_t newsize = area->size;

					/* area cannot be larger than AREA_MAX_SIZE */
					if (area->size > AREA_MAX_SIZE)
						newsize = AREA_MAX_SIZE; 
					/* extending by more than 2 pages is waste (or maybe not?) */
					if (area->size - oldsize > PAGE_SIZE * 2)
						newsize = PAGE_SIZE * 2 + oldsize;
					/* if new area is to big then shrink it */
					if (area->size > newsize)
						areamgr_shrink_area(self->areamgr, &area, SIZE_IN_PAGES(newsize), side);

					area->manager = AREA_MGR_EQSBMGR;
					area_touch(area);

					mgr = sb_mgr_expand(mgr, (newsize - oldsize) / SB_SIZE, side);
				}

				if (area_end(area) == area_begining(area->local.next)) {
					DEBUG("Area %.8x should be merged with area %.8x\n", (uint32_t)area, (uint32_t)area->local.next);

					sb_mgr_t *oldmgr = mgr;
					sb_mgr_t *newmgr = NULL;

					arealst_remove_area(&self->arealst, area->local.next, DONTLOCK);
					area = arealst_join_area(&self->areamgr->global, area, area->global.next, LOCK);

					newmgr = sb_mgr_from_area(area);

					newmgr->free += oldmgr->free;
					newmgr->all  += oldmgr->all;

					uint32_t i;

					for (i = 0; i < 4; i++) {
						sb_list_join(&newmgr->groups[i], &oldmgr->groups[i]);
						sb_list_join(&newmgr->nonempty[i], &oldmgr->nonempty[i]);
					}

					sb_t *oldsb  = sb_get_from_address(mgr);

					mgr = newmgr;

					/* free some unused blocks */
					uint32_t old_blocks = sb_get_blocks(oldsb);

					oldsb->size = (SB_SIZE >> 3) - 1;

					uint32_t blocks = sb_get_blocks(oldsb);

					DEBUG("Freeing %u unused blocks in SB at $%.8x\n", blocks - old_blocks, (uint32_t)oldsb);

					uint32_t freeblocks = oldsb->fblkcnt;

					while (blocks > old_blocks)
						sb_free(oldsb, --blocks);

					if (freeblocks == 0)
						sb_list_push(&mgr->nonempty[oldsb->blksize], oldsb);
				}

				sb = sb_mgr_alloc(mgr, blksize);
			}

			if (sb != NULL)
				break;

			area = area->local.next;
		}

		if (sb == NULL) {
			DEBUG("No adjacent areas found - try to create new superblocks' manager.\n");
			/* second attempt: create new superblocks' manager */
			area_t *newarea = areamgr_alloc_area(self->areamgr, 1);

			if (newarea) {
				newarea->manager = AREA_MGR_EQSBMGR;

				arealst_insert_area_by_addr(&self->arealst, (void *)newarea, DONTLOCK);

				mgr = sb_mgr_from_area(newarea);

				sb_mgr_init(mgr);
				sb_mgr_add(mgr, area_begining(newarea), newarea->size / SB_SIZE);

				sb = sb_mgr_alloc(mgr, blksize);
			} else {
				DEBUG("Failed to create new superblocks' manager :(\n");
			}
		}
	}

	void *memory = NULL;

	if (sb != NULL) {
		int32_t index = sb_alloc(sb);

		if (index >= 0)
			memory = (void *)((uint32_t)sb_get_data(sb) + index * ((blksize + 1) << 3));

		if (sb->fblkcnt == 0)
			sb_list_remove(&mgr->nonempty[sb->blksize], sb);

	}

	arealst_unlock(&self->arealst);

	return memory;
}/*}}}*/

/**
 * Return a block to equally-sized blocks' manager.
 *
 * @param self		equally-sized blocks' manager structure
 * @param memory	address of blocks to be freed
 * @return			TRUE if block was managed by this manager
 */

bool eqsbmgr_free(eqsbmgr_t *self, void *memory)/*{{{*/
{
	DEBUG("\033[37;1mRequested to free block at $%.8x.\033[0m\n", (uint32_t)memory);

	sb_mgr_t *mgr = NULL;

	/* browse list of managed areas */
	arealst_wrlock(&self->arealst);

	area_t *area = (area_t *)self->arealst.local.next;

	while (!area_is_guard(area)) {
		if ((area_begining(area) <= memory) && (memory <= area_end(area))) {
			mgr = sb_mgr_from_area(area);
			break;
		}

		area = area->local.next;
	}

	/* SBs' manager found - free block */
	if (mgr != NULL) {
		sb_t *sb = sb_get_from_address(memory);

		uint8_t i = (uint32_t)(memory - sb_get_data(sb)) / ((sb->blksize + 1) << 3);

		sb_free(sb, i);

		uint8_t blocks = sb_get_blocks(sb);

		if (blocks == sb->fblkcnt) {
			sb_list_remove(&mgr->nonempty[sb->blksize], sb);
			sb_mgr_free(mgr, sb);
		}

		if (sb->fblkcnt == 1)
			sb_list_push(&mgr->nonempty[sb->blksize], sb);

		/* Check if there are no pages to free */
		if (mgr->groups[3].first) {
			DEBUG("freecnt = %d, pages free = %d\n", mgr->free, mgr->groups[3].sbcnt);

			if ((mgr->free > 4) && (mgr->groups[3].sbcnt > 0)) {
				sb_t *tmp = (sb_t *)area_begining(area);

				DEBUG("Try to remove from the begining of area. Check superblock at $%.8x.\n", (uint32_t)tmp);

				if ((tmp->fblkcnt == 127) && (tmp->blksize == 3)) {
					sb_list_remove(&mgr->groups[3], tmp);

					areamgr_shrink_area(self->areamgr, &area, SIZE_IN_PAGES(area->size) - 1, LEFT);

					mgr->free -= 4;
					mgr->all  -= 4;

					if (verbose) sb_mgr_print(mgr);
				}
			}

			if ((mgr->free > 4) && (mgr->groups[3].sbcnt > 0)) {
				sb_t *to_free = (sb_t *)((uint32_t)area_end(area) - PAGE_SIZE);

				DEBUG("Try to remove from the end of area. Check superblock at $%.8x.\n", (uint32_t)to_free);

				if ((to_free->fblkcnt == 127) && (to_free->blksize == 3)) {
					sb_mgr_t *new_mgr    = (sb_mgr_t *)((uint32_t)to_free - (sizeof(area_t) + sizeof(sb_mgr_t)));
					sb_t     *new_lastsb = sb_get_from_address(new_mgr);
					sb_t     *lastsb	 = sb_get_from_address(mgr);

					if (new_lastsb->fblkcnt == 127) {
						/* check if there is space fo area structure and SBs' manager structure */
						sb_list_remove(&mgr->groups[3], to_free);

						memcpy(new_mgr, mgr, sizeof(sb_mgr_t));

						mgr = new_mgr;

						mgr->free -= 4;
						mgr->all  -= 4;

						new_lastsb->size = lastsb->size;

						areamgr_shrink_area(self->areamgr, &area, SIZE_IN_PAGES(area->size) - 1, RIGHT);

						if (verbose) sb_mgr_print(mgr);
					}
				}
			}
		}
	}

	arealst_unlock(&self->arealst);

	return (mgr != NULL);
}/*}}}*/

/**
 * Resize allocated block. In fact it's only checked if block will fit
 * in exactly the same place without losing memory.
 */

bool eqsbmgr_realloc(eqsbmgr_t *self, void *memory, uint32_t new_size)/*{{{*/
{
	DEBUG("\033[37;1mResizing block at $%.8x to %u bytes.\033[0m\n", (uint32_t)memory, new_size);

	assert(new_size <= 32);

	uint8_t  new_blksize = (new_size - 1) >> 3;
	sb_mgr_t *mgr = NULL;

	/* browse list of managed areas */
	arealst_rdlock(&self->arealst);

	area_t *area = (area_t *)self->arealst.local.next;

	while (!area_is_guard(area)) {
		if ((area_begining(area) <= memory) && (memory <= area_end(area))) {
			mgr = sb_mgr_from_area(area);
			break;
		}

		area = area->local.next;
	}

	bool res = FALSE;
		
	if (mgr != NULL) {
		/* check if there is need to resize block */
		sb_t *sb = sb_get_from_address(memory);

		res = (sb->blksize == new_blksize);
	} else {
		DEBUG("Block at $%.8x not found!\n", (uint32_t)memory);
		abort();
	}

	arealst_unlock(&self->arealst);

	return res;
}/*}}}*/

/**
 * Print all internal structures of equally-sized blocks' manager.
 * 
 * @param self		equally-sized bloks' manager structure
 */

void eqsbmgr_print(eqsbmgr_t *self)/*{{{*/
{
	arealst_rdlock(&self->arealst);

	area_t *area = (area_t *)&self->arealst;

	fprintf(stderr, "\033[1;36m eqsbmgr at $%.8x [%d areas]:\033[0m\n",
			(uint32_t)self, self->arealst.areacnt);

	bool error = FALSE;
	uint32_t areacnt = 0;

	while (TRUE) {
		area_valid(area);

		if (!area_is_guard(area)) {
			fprintf(stderr, "\033[1;31m  $%.8x - $%.8x : %8d : $%.8x : $%.8x\033[0m\n",
					(uint32_t)area_begining(area), (uint32_t)area_end(area), area->size,
					(uint32_t)area->local.prev, (uint32_t)area->local.next);

			sb_mgr_t *mgr = sb_mgr_from_area(area);

			sb_mgr_print(mgr);

			assert(area->manager == AREA_MGR_EQSBMGR);
		}
		else
			fprintf(stderr, "\033[1;33m  $%.8x %11s : %8s : $%.8x : $%.8x\033[0m\n",
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

