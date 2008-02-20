/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Random malloc / free test
 */

#include "memmgr.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#define MAX_THREADS			1024

#define MAX_BLOCK_NUM		(1 << 16)
#define MAX_MEM_USED		(1 << 20)
#define MAX_BLOCK_SIZE		(1 << 5)
#define MAX_REALLOC_SIZE	(1 << 4)
#define MAX_ALIGN_BITS		0
#define MAX_OPS_STREAM		(1 << 7)

#define PPB_REALLOC		0
#define PPB_MEMALIGN	0
#define PPB_ALLOC		10
#define PPB_FREE		6

#define LEN_REALLOC		0
#define LEN_MEMALIGN	0
#define LEN_ALLOC		6
#define LEN_FREE		10

#if (PPB_REALLOC + PPB_ALLOC_ALIGNED + PPB_ALLOC + PPB_FREE) != 16
#error "Check propabilites for operations!"
#endif

#if (LEN_REALLOC + LEN_ALLOC_ALIGNED + LEN_ALLOC + LEN_FREE) != 16
#error "Check length for operations!"
#endif

/**
 * Generate two random numbers with normal distribution.
 */

void gaussian(double *p, double *q)
{
	double x1, x2, w;

	do {
		x1 = 2.0 * drand48() - 1.0;
		x2 = 2.0 * drand48() - 1.0;
		w = x1 * x1 + x2 * x2;
	} while (w >= 1.0);

	w = sqrt((-2.0 * log(w)) / w);

	*p = x1 * w;
	*q = x2 * w;
}

/**
 * Program usage printing.
 */

static void usage(char *progname)
{
	printf("Usage: %s [parameters]\n"
		   "\n"
		   "Parameters:\n"
		   "  -s seed   - seed for pseudo random number generator [mandatory]\n"
		   "  -c opsnum - number of memory blocks' operations (at least 100) [mandatory]\n"
		   "  -t test   - which allocator to test (test = 0-7) [default: 7 (all)]\n"
		   "\n", progname);

	exit(EXIT_FAILURE);
}

/**
 * Definition of structures for allocator tester.
 */

struct block
{
	void    *ptr;
	int32_t size;
};

typedef struct block block_t;

struct block_array
{
	block_t array[MAX_BLOCK_NUM];
	int32_t last;

	pthread_mutex_t mutex;
};

typedef struct block_array block_array_t;

struct block_class
{
	uint32_t min_size;
	uint32_t max_size;
};

typedef struct block_class block_class_t;

/**
 * Structures for allocator tester.
 */

// static block_range_t block_ranges[3] = { {1, 32}, {33, 32767}, {32768, 1 << 20} };
static block_array_t blocks;
static memmgr_t *mm;

/**
 * Allocates and locks space for block.
 *
 * @param self	pointer to blocks' array structure
 * @return		number of block or -1 if array is full
 */

static void block_array_init()
{
	memset(&blocks, 0, sizeof(block_array_t));

	blocks.last = -1;

	/* Initialize locking mechanism */
	pthread_mutexattr_t mutex_attr;

	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setpshared(&mutex_attr, 1);
	pthread_mutex_init(&blocks.mutex, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);
}

static bool block_array_alloc(void *ptr, int32_t size)
{
	bool result = FALSE;

	pthread_mutex_lock(&blocks.mutex);

	if (blocks.last < MAX_BLOCK_NUM) {
		blocks.array[blocks.last + 1].ptr  = ptr;
		blocks.array[blocks.last + 1].size = size;

		blocks.last++;

		DEBUG("Allocated %u block.\n", blocks.last);

		result = TRUE;
	}

	pthread_mutex_unlock(&blocks.mutex);

	return result;
}

static bool block_array_free(void **ptr, int32_t *size)
{
	bool result = FALSE;

	pthread_mutex_lock(&blocks.mutex);

	if (blocks.last >= 0) {
		int32_t i = rand() % (blocks.last + 1);

		*ptr  = blocks.array[i].ptr;
		*size = blocks.array[i].size;

		if (blocks.last != i) 
			blocks.array[i] = blocks.array[blocks.last];

		blocks.array[blocks.last].ptr  = NULL;
		blocks.array[blocks.last].size = 0;

		blocks.last--;

		DEBUG("Freed %u block, last block at %d.\n", i, blocks.last);

		result = TRUE;
	} else {
		*ptr  = NULL;
		*size = 0;
	}

	pthread_mutex_unlock(&blocks.mutex);

	return result;
}


/**
 * Allocator tester.
 */

static void *memmgr_test(void *args)
{
	int32_t ops      = ((uint32_t *)args)[0];
	int32_t testtype = ((uint32_t *)args)[1];
	bool    verbose  = ((uint32_t *)args)[2];

	int32_t opcnt = 0;

	while (opcnt < ops) {
		uint32_t ppb = rand() & 15;
		uint32_t opstream = rand() & (MAX_OPS_STREAM - 1);
		uint32_t opcode = 0;

		if ((ppb <= PPB_REALLOC) && (PPB_REALLOC > 0)) {
			opcode = 0;
			opstream = (opstream * LEN_REALLOC) >> 4;
		} else if ((ppb <= PPB_REALLOC + PPB_MEMALIGN) && (PPB_REALLOC > 0)) {
			opcode = 1;
			opstream = (opstream * LEN_MEMALIGN) >> 4;
		} else if ((ppb <= PPB_REALLOC + PPB_MEMALIGN + PPB_ALLOC) && (PPB_ALLOC > 0)) {
			opcode = 2;
			opstream = (opstream * LEN_ALLOC) >> 4;
		} else if (PPB_FREE > 0) {
			opcode = 3;
			opstream = (opstream * LEN_FREE) >> 4;
		}

		while (opstream--) {
			int32_t size, alignment;
			void *ptr;

			if (verbose)
				memmgr_print(mm);

			switch (opcode) {
				/* case for mm_realloc */
				case 0:
					if (block_array_free(&ptr, &size)) {
						size += (rand() & (MAX_REALLOC_SIZE - 1)) - (MAX_REALLOC_SIZE >> 1);

						if (memmgr_realloc(mm, ptr, size)) {
							DEBUG("realloc(%p, %u)\n", ptr, size);
							opcnt++;
						} else {
							DEBUG("memmgr_free: could not free block!\n");
							abort();
						}

						block_array_alloc(ptr, size);
					}
					break;

				/* case for mm_alloc_aligned */
				case 1:
					size = rand() & (MAX_BLOCK_SIZE - 1);
					alignment = 1 << (rand() % (MAX_ALIGN_BITS + 4));

					if ((ptr = memmgr_alloc(mm, (size > 0) ? size : 1, alignment))) {
						DEBUG("memalign(%d, %d) = %p\n", size, alignment, ptr);
						opcnt++;
					} else {
						DEBUG("memmgr_alloc aligned: out of memory!\n");
						abort();
					}

					block_array_alloc(ptr, size);
					break;

				/* case for memmgr_alloc */
				case 2:
					size = rand() & (MAX_BLOCK_SIZE - 1);

					if ((ptr = memmgr_alloc(mm, (size > 0) ? size : 1, 0))) {
						DEBUG("malloc(%d) = %p\n", size, ptr);
						opcnt++;
					} else {
						DEBUG("memmgr_alloc: out of memory!\n");
						abort();
					}

					block_array_alloc(ptr, size);
					break;

				/* case for memmgr_free */
				default:
					if (block_array_free(&ptr, &size)) {
						if (memmgr_free(mm, ptr)) {
							DEBUG("free(%p, %u)\n", ptr, size);
							opcnt++;
						} else {
							DEBUG("memmgr_free: could not free block!\n");
							abort();
						}
					}
					break;
			}
		}
	}
	
	return NULL;
}

/**
 * Program entry.
 */

bool verbose = FALSE;

int main(int argc, char **argv)
{
	int32_t seed     = -1;
	int32_t ops      = -1;
	int32_t testtype = 7;
	int32_t threads  = 1;
	char c;

	opterr = 0;

	while ((c = getopt(argc, argv, "n:c:t:s:v")) != -1) {
		if (c == 's') {
			char *tmp;

			seed = strtol(optarg, &tmp, 10);

			if (!(*optarg != '\0' && *tmp == '\0'))
				usage(argv[0]);
		} else if (c == 'c') {
			char *tmp;

			ops = strtol(optarg, &tmp, 10);

			if (!(*optarg != '\0' && *tmp == '\0'))
				usage(argv[0]);

			if (ops < 100)
				usage(argv[0]);
		} else if (c == 't') {
			char *tmp;

			testtype = strtol(optarg, &tmp, 10);

			if (!(*optarg != '\0' && *tmp == '\0'))
				usage(argv[0]);

			if (testtype < 0 || testtype > 3)
				usage(argv[0]);
		} else if (c == 'n') {
			char *tmp;

			threads = strtol(optarg, &tmp, 10);

			if (!(*optarg != '\0' && *tmp == '\0'))
				usage(argv[0]);

			if (threads < 1 || threads > MAX_THREADS)
				usage(argv[0]);
		} else if (c == 'v') {
			verbose = TRUE;
		} else {
			usage(argv[0]);
		}
	}

	if (seed < 0 || ops < 0)
		usage(argv[0]);

	/* initialize test */
	srand(seed);

	mm = memmgr_init();

	block_array_init();

	uint32_t args[3] = { ops, testtype, verbose };

	/* test allocators ! */
	if (threads > 1)
	{
		uint32_t i;

		pthread_t threadid[1024];

		for (i = 0; i < threads; i++) {
			pthread_create(&threadid[i], NULL, memmgr_test, args);
			fprintf(stderr, "Started thread $%.8x.\n", (uint32_t)threadid[i]);
		}

		for (i = 0; i < threads; i++) {
			pthread_join(threadid[i], NULL);
			fprintf(stderr, "Finished thread $%.8x.\n", (uint32_t)threadid[i]);
		}
	} else {
		memmgr_test(args);
	}

	memmgr_print(mm);

	return 0;
}
