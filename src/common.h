#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <nana.h>
#include <stdio.h>

typedef enum { FALSE, TRUE } bool;
typedef enum { NONE, LEFT, RIGHT, BOTH } direction_t;

typedef enum { DONTLOCK, LOCK } locking_t;

extern bool verbose;

#define ALIGN_UP(data, size)	(((uint32_t)(data) + ((size) - 1)) & ~((size) - 1))
#define ALIGN_DOWN(data, size)	((uint32_t)(data) & ~((size) - 1))
#define ALIGN(data, size)		ALIGN_UP((data), (size))

#if VERBOSE == 1
#define DEBUG(format, args...) if (verbose) fprintf(stderr, "\033[1m%s:%d\033[0m " format, __func__, __LINE__, ##args)
#else
#define DEBUG(format, args...)
#endif

#define PANIC(format, args...) { fprintf(stderr, "\033[1;37m%s:%d \033[0;4m" format "\033[0m\n", __func__, __LINE__, ##args); abort(); }

#define offsetof(type, member)	__builtin_offsetof(type, member)

/*
 * A few constants meaningful for Pentium 4 cache facts.
 */

#define L1_SECTOR_SIZE	32
#define L1_LINE_SIZE	64
#define L2_SECTOR_SIZE	32
#define L2_LINE_SIZE	64
#define L3_LINE_SIZE	128

/*
 * Simple algorithm for calculating checksum.
 */

static inline uint16_t checksum(uint16_t *data, uint32_t words)
{
	uint16_t sum = 0;

	while (words > 0) {
		sum	^= *data++;
		words--;
	}

	return sum;
}

static inline void hexdump(void *data, uint32_t size)
{
	uint32_t i;

	fprintf(stderr, "Dumping %u bytes at %.8x:", size, (uint32_t)data);

	for (i = 0; i < size; i++) {
		if (i % 32 == 0)
			fprintf(stderr, "\n  ");

		fprintf(stderr, "%.2x ", ((uint8_t *)data)[i]);
	}

	fprintf(stderr, "\n");
}

#endif
