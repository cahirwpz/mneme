#ifndef __BLKMAN_AO_H
#define __BLKMAN_AO_H

#include "common.h"
#include "areaman.h"
#include <stdio.h>

#define MB_GRANULARITY_BITS		4
#define MB_GRANULARITY			(1 << MB_GRANULARITY_BITS)
#define MB_GRANULARITY_MASK		(MB_GRANULARITY - 1)

#define MB_FLAG_USED	1
#define MB_FLAG_PAD		2
#define MB_FLAG_FIRST	4
#define MB_FLAG_LAST	8
#define MB_FLAG_GUARD	16

/* Memory block structure */

struct memblock
{
	uint16_t checksum;
	uint16_t flags;
	uint32_t size;

	struct memblock *next;	/* if (mb_is_guard(blk)) 'next' points to first free block or is loopback pointer */
	struct memblock *prev;	/* if (mb_is_guard(blk)) 'prev' points always to last block */
} __attribute__((aligned(MB_GRANULARITY)));

typedef struct memblock memblock_t;

/* Few inlines to make code more readable :) */

static inline bool mb_is_guard(memblock_t *blk) {
	return (blk->flags & MB_FLAG_GUARD);
}

static inline bool mb_is_used(memblock_t *blk) {
	return (blk->flags & MB_FLAG_USED);
}

static inline bool mb_is_last(memblock_t *blk) {
	return (blk->flags & MB_FLAG_LAST);
}

static inline memblock_t *mb_from_memarea(memarea_t *area) {
	return (memblock_t *)ALIGN((uint32_t)(area + 1), MB_GRANULARITY);
}

/* Calculate checksum of memory block structure. */

static uint16_t mb_checksum(memblock_t *blk)
{
	int bytes = (mb_is_used(blk) ? offsetof(memblock_t, next) : sizeof(memblock_t)) - sizeof(uint16_t);

	return (((uint32_t)blk) >> 16) ^ (((uint32_t)blk) & 0xFFFF) ^ checksum((uint16_t *)&blk->flags, bytes >> 1);
}

/* Recalculate memory block checksum. */

static inline void mb_touch(memblock_t *blk)
{
	blk->checksum = mb_checksum(blk);
}

/* Check corectness of memory block checksum. */

static inline void mb_valid(memblock_t *blk)
{
	if (mb_checksum(blk) != blk->checksum) {
		fprintf(stderr, "invalid block: [$%.8x; %u; $%.2x]\n", (uint32_t)blk, blk->size, blk->flags);
		abort();
	}
}

/* Function prototypes */
void mb_print(memblock_t *guard);
void mb_init(memblock_t *guard, uint32_t size);
void *mb_alloc(memblock_t *guard, uint32_t size, bool from_last);
void mb_free(memblock_t *guard, void *memory);

/* Procedures used in conjuction with operations on memory areas */
uint32_t mb_list_can_shrink(memblock_t *guard);
void mb_list_shrink(memblock_t *guard, uint32_t pages);
void mb_list_expand(memblock_t *guard, uint32_t pages);
void mb_list_merge(memblock_t *first_guard, memblock_t *second_guard);

#endif
