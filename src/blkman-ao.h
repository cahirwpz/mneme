#ifndef __BLKMAN_AO_H
#define __BLKMAN_AO_H

#include "common.h"
#include "areaman.h"
#include <stdio.h>

#define MB_GRANULARITY_BITS		3
#define MB_GRANULARITY			(1 << MB_GRANULARITY_BITS)
#define MB_GRANULARITY_MASK		(MB_GRANULARITY - 1)

#define MB_FLAG_USED	1
#define MB_FLAG_PAD		2
#define MB_FLAG_FIRST	4
#define MB_FLAG_LAST	8
#define MB_FLAG_GUARD	16

/* Memory block structure */

struct memory_block
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;
} __attribute__((aligned(MB_GRANULARITY)));

typedef struct memory_block mb_t;

/* Free memory block structure */

struct memory_block_free
{
	struct memory_block;

	struct memory_block_free *next;
	struct memory_block_free *prev;
} __attribute__((aligned(MB_GRANULARITY)));

typedef struct memory_block_free mb_free_t;

/* Memory blocks' list */

struct memory_block_list
{
	struct memory_block_free;

	uint16_t blkcnt;
	uint16_t ublkcnt;
	uint32_t fmemcnt;
} __attribute__((aligned(MB_GRANULARITY)));

typedef struct memory_block_list mb_list_t;

/* Few inlines to make code more readable :) */

#define mb_is_guard(blk) mb_is_guard_internal((mb_t *)(blk))

static inline bool mb_is_guard_internal(mb_t *blk) {
	return (blk->flags & MB_FLAG_GUARD);
}

#define mb_is_used(blk) mb_is_used_internal((mb_t *)(blk))

static inline bool mb_is_used_internal(mb_t *blk) {
	return (blk->flags & MB_FLAG_USED);
}

#define mb_is_first(blk) mb_is_first_internal((mb_t *)(blk))

static inline bool mb_is_first_internal(mb_t *blk) {
	return (blk->flags & MB_FLAG_FIRST);
}

#define mb_is_last(blk) mb_is_last_internal((mb_t *)(blk))

static inline bool mb_is_last_internal(mb_t *blk) {
	return (blk->flags & MB_FLAG_LAST);
}

static inline mb_list_t *mb_list_from_memarea(area_t *area) {
	return (mb_list_t *)ALIGN((uint32_t)(area + 1), MB_GRANULARITY);
}

/* Calculate checksum of memory block structure. */

static uint16_t mb_checksum(mb_t *blk)
{
	int bytes;

	if (mb_is_used(blk))
		bytes = sizeof(mb_t) - sizeof(uint16_t);
	else if (mb_is_guard(blk))
		bytes = sizeof(mb_list_t) - sizeof(uint16_t);
	else
		bytes = sizeof(mb_free_t) - sizeof(uint16_t);

	return (((uint32_t)blk) >> 16) ^ (((uint32_t)blk) & 0xFFFF) ^ checksum((uint16_t *)&blk->flags, bytes >> 1);
}

/* Recalculate memory block checksum. */

#define mb_touch(blk) mb_touch_internal((mb_t *)(blk))

static inline void mb_touch_internal(mb_t *blk)
{
	blk->checksum = mb_checksum(blk);
}

/* Check corectness of memory block checksum. */

#define mb_valid(blk) mb_valid_internal((mb_t *)(blk))

static inline void mb_valid_internal(mb_t *blk)
{
	if (mb_checksum(blk) != blk->checksum) {
		fprintf(stderr, "invalid block: [$%.8x; %u; $%.2x]", (uint32_t)blk, blk->size, blk->flags);

		if (!mb_is_used(blk)) {
			mb_free_t *fblk = (mb_free_t *)blk;

			fprintf(stderr, " [prev: $%.8x; next: $%.8x]", (uint32_t)fblk->prev, (uint32_t)fblk->next);
		}

		if (mb_is_guard(blk)) {
			mb_list_t *list = (mb_list_t *)blk;

			fprintf(stderr, " [fmemcnt: %u; blkcnt: %u; ublkcnt: %u]",
					list->fmemcnt, list->blkcnt, list->ublkcnt);
		}

		fprintf(stderr, "\n");

		abort();
	}
}

/* Function prototypes */
void mb_print(mb_list_t *list);
void mb_init(mb_list_t *list, uint32_t size);
void *mb_alloc(mb_list_t *list, uint32_t size, bool from_last);
void *mb_alloc_aligned(mb_list_t *list, uint32_t size, uint32_t alignment);
bool mb_resize(mb_list_t *list, void *memory, uint32_t new_size);
mb_free_t *mb_free(mb_list_t *list, void *memory);

/* Procedures used in conjuction with operations on memory areas */
uint32_t mb_list_can_shrink_at_beginning(mb_list_t *list, uint32_t space);
uint32_t mb_list_can_shrink_at_end(mb_list_t *list);

void mb_list_shrink_at_beginning(mb_list_t **list, uint32_t pages, uint32_t space);
void mb_list_shrink_at_end(mb_list_t *list, uint32_t pages);

uint32_t mb_list_find_split(mb_list_t *list, mb_free_t **to_split, void **cut, uint32_t space);
mb_list_t *mb_list_split(mb_list_t *first, mb_free_t *to_split, uint32_t pages, uint32_t space);

void mb_list_expand(mb_list_t *guard, uint32_t pages);
void mb_list_merge(mb_list_t *first, mb_list_t *second, uint32_t space);

#endif
