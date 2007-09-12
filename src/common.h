#ifndef __COMMON_H
#define __COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

typedef enum { FALSE, TRUE } bool;

#define ALIGN(data, size)		(((uint32_t)data + (size - 1)) & ~(size - 1))

#if VERBOSE == 1
#define DEBUG(format, args...) fprintf(stderr, "%s:%d " format, __func__, __LINE__, ##args)
#else
#define DEBUG(format, args...)
#endif

#define offsetof(type, member)	__builtin_offsetof(type, member)

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

#endif
