/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Random malloc / free test
 */

#include "memmgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#define MAX_THREADS			1024

#define MAX_BLOCK_NUM		(1 << 18)
#define MAX_MEM_USED		(1 << 28)		/* 256 MiB */
#define MAX_BLOCK_CLASS		3

#define MIN_ALIGN_BITS		4
#define MAX_ALIGN_BITS		16

#define MAX_OPS_STREAM		128

/**
 * Global data.
 */

static struct {
	int32_t	ops;
	int32_t	type;
	double	malloc_pbb;
	double	align_pbb;
	double  grow_pbb;
	double  shrink_pbb;
} test = { -1, 7, 0.5, 0.0, 0.0, 0.0 };

bool verbose = FALSE;
bool print_at_iter = FALSE;

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
		   "  -s seed    - seed for pseudo random number generator [mandatory]\n"
		   "  -c opsnum  - number of memory blocks' operations (at least 100) [mandatory]\n"
		   "  -t test    - which allocator to test (test = 0-3) [default: 0 (all)]\n"
		   "  -n threads - how many threads to run [default: 1]\n"
		   "  -M pbb     - pbb of block being allocated vs. being freed [default: 0.5, range: 0.1 - 0.9]\n"
		   "  -G pbb     - pbb of malloc being replaced by realloc which will \033[4mgrow\033[0m block [default: 0.0, max: 0.5]\n"
		   "  -S pbb     - pbb of free being replaced by realloc which will \033[4mshrink\033[0m block [default: 0.0, max: 0.5]\n"
		   "  -A pbb     - pbb of malloc with \033[4malignment\033[0m contraint [default: 0.0, max: 0.5]\n"
		   "  -p         - print structures of memory allocator at every iteration [default: no]\n"
		   "  -v         - be verbose [default: no]\n"
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
	int32_t usedmem;

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

static block_class_t block_classes[MAX_BLOCK_CLASS] = { {1, 32}, {33, 32767}, {32768, 131072} };
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
	blocks.usedmem = 0;

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

	if ((blocks.last < MAX_BLOCK_NUM) && (blocks.usedmem + size < MAX_MEM_USED) &&
		(size <= block_classes[2].max_size))
	{
		blocks.array[blocks.last + 1].ptr  = ptr;
		blocks.array[blocks.last + 1].size = size;

		blocks.last++;
		blocks.usedmem += size;

		DEBUG("Allocated block no. %u [$%.8x, %u]. Last block at %d. Used memory: %u.\n",
			  blocks.last, (uint32_t)ptr, size, blocks.last, blocks.usedmem);

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
		blocks.usedmem -= *size;

		DEBUG("Freed block no. %u [$%.8x, %u]. Last block at %d. Used memory: %u.\n",
			  i, (uint32_t)*ptr, *size, blocks.last, blocks.usedmem);

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
	int32_t opcnt = 0;

	while (opcnt < test.ops) {
		double   pbb = drand48();
		double   len = drand48();

		uint32_t opstream, optype;

		if (pbb < test.malloc_pbb) {
			optype   = 0;
			opstream = (uint32_t)(len * MAX_OPS_STREAM * (1.0 - test.malloc_pbb));
		} else {
			optype   = 1;
			opstream = (uint32_t)(len * MAX_OPS_STREAM * test.malloc_pbb);
		}

		while (opstream--) {
			int32_t size, alignment;
			void *ptr;

			if (print_at_iter)
				memmgr_print(mm);

			pbb = drand48();

			/* case for malloc / realloc (grow) / memalign */
			if (optype == 0) {
				if (pbb < test.grow_pbb) {
					DEBUG("Case for realloc (grow).\n");
					if (block_array_free(&ptr, &size)) {
						int32_t delta = (int32_t)(drand48() * size * 0.5);

						if (delta < 8)
							delta = 8;

						if (size + delta > block_classes[2].max_size)
							delta = block_classes[2].max_size - size;

						if (memmgr_realloc(mm, ptr, size + delta)) {
							size += delta;

							DEBUG("realloc(%p, %u)\n", ptr, size);
							opcnt++;
						}

						if (!block_array_alloc(ptr, size)) {
							memmgr_print(mm);
							DEBUG("realloc grow: cannot store block.\n");
							abort();
						}
					}
				} else {
					pbb -= test.grow_pbb;

					if (pbb < test.align_pbb) {
						DEBUG("Case for memalign.\n");
						alignment = rand() % MAX_ALIGN_BITS;

						if (alignment < MIN_ALIGN_BITS)
							alignment = MIN_ALIGN_BITS;

						alignment = 1 << alignment;
					} else {
						DEBUG("Case for malloc.\n");
						alignment = 0;
					}

					pbb = drand48();

					if (test.type == 0) {
						double range = block_classes[2].max_size - block_classes[0].min_size;

						size = (uint32_t)(range * pbb) + block_classes[0].min_size;
					} else if (test.type == 2) {
						double pbb2 = drand48();

						gaussian(&pbb, &pbb2);

						pbb = fabs(pbb) / 16;

						if (pbb > 1.0)
							pbb = 1.0;

						double range = block_classes[1].max_size - block_classes[1].min_size;

						size = (uint32_t)(range * fabs(pbb)) + block_classes[1].min_size;
					} else {
						uint32_t i = test.type - 1;
						double   range = block_classes[i].max_size - block_classes[i].min_size;

						size = (uint32_t)(range * pbb) + block_classes[i].min_size;
					}

					if ((ptr = memmgr_alloc(mm, (size > 0) ? size : 1, alignment))) {
						if (alignment > 0) {
							assert(((uint32_t)ptr & (alignment - 1)) == 0);
							DEBUG("memalign(%d, %d) = %p\n", size, alignment, ptr);
						} else {
							DEBUG("malloc(%d) = %p\n", size, ptr);
						}
						opcnt++;
					} else {
						memmgr_print(mm);
						DEBUG("alloc: out of memory!\n");
						abort();
					}

					if (!block_array_alloc(ptr, size)) {
						memmgr_print(mm);
						DEBUG("alloc: cannot store block.\n");
						abort();
					}
				}
			}
			
			/* case for free / realloc (shrink) */
			if (optype == 1) {
				if (pbb < test.shrink_pbb) {
					DEBUG("Case for realloc (shrink).\n");
					if (block_array_free(&ptr, &size)) {
						int32_t delta = (int32_t)(drand48() * size * 0.5);

						if (delta < 8)
							delta = 8;

						if (size <= delta)
							delta = 0;


						if (memmgr_realloc(mm, ptr, size - delta)) {
							size -= delta;

							DEBUG("realloc(%p, %u)\n", ptr, size);
							opcnt++;
						} else {
							memmgr_print(mm);
							DEBUG("realloc shrink: could not shrink block!\n");
							abort();
						}

						if (!block_array_alloc(ptr, size)) {
							memmgr_print(mm);
							DEBUG("realloc shrink: cannot store block.\n");
							abort();
						}
					}
				} else {
					DEBUG("Case for free.\n");
					if (block_array_free(&ptr, &size)) {
						if (memmgr_free(mm, ptr)) {
							DEBUG("free(%p, %u)\n", ptr, size);
							opcnt++;
						} else {
							memmgr_print(mm);
							DEBUG("free: could not free block!\n");
							abort();
						}
					}
				}
			}
		}
	}
	
	return NULL;
}

/**
 * String to number conversion.
 */

bool strtoint(char *str, int32_t *numptr)
{
	char *tmp = NULL;
	
	*numptr = strtol(str, &tmp, 10);

	return (*str != '\0' && *tmp == '\0');
}

bool strtodouble(char *str, double *numptr)
{
	char *tmp = NULL;
	
	*numptr = strtod(str, &tmp);

	return (*str != '\0' && *tmp == '\0');
}

/**
 * Program entry.
 */

int main(int argc, char **argv)
{
	int32_t seed		= -1;
	int32_t threads		= 1;
	char c;

	opterr = 0;

	while ((c = getopt(argc, argv, "n:c:t:s:M:A:G:S:pv")) != -1) {
		switch (c) {
			case 's':
				if (!strtoint(optarg, &seed))
					usage(argv[0]);
				break;

			case 'c':
				if (!strtoint(optarg, &test.ops))
					usage(argv[0]);
				if (test.ops < 100)
					usage(argv[0]);
				break;

			case 't':
				if (!strtoint(optarg, &test.type))
					usage(argv[0]);
				if (test.type < 0 || test.type > 3)
					usage(argv[0]);
				break;

			case 'n':
				if (!strtoint(optarg, &threads))
					usage(argv[0]);
				if (threads < 1 || threads > MAX_THREADS)
					usage(argv[0]);
				break;

			case 'M':
				if (!strtodouble(optarg, &test.malloc_pbb))
					usage(argv[0]);
				if ((test.malloc_pbb < 0.1) && (test.malloc_pbb > 0.9))
					usage(argv[0]);
				break;

			case 'A':
				if (!strtodouble(optarg, &test.align_pbb))
					usage(argv[0]);
				if ((test.align_pbb < 0.0) && (test.align_pbb > 0.5))
					usage(argv[0]);
				break;

			case 'G':
				if (!strtodouble(optarg, &test.grow_pbb))
					usage(argv[0]);
				if ((test.grow_pbb < 0.0) && (test.grow_pbb > 0.5))
					usage(argv[0]);
				break;

			case 'S':
				if (!strtodouble(optarg, &test.shrink_pbb))
					usage(argv[0]);
				if ((test.shrink_pbb < 0.0) && (test.shrink_pbb > 0.5))
					usage(argv[0]);
				break;

			case 'v':
				verbose = TRUE;
				break;

			case 'p':
				print_at_iter = TRUE;
				break;

			default:
				usage(argv[0]);
				break;
		}
	}

	if ((seed < 0) || (test.ops < 0))
		usage(argv[0]);

	/* initialize random numbers generator */
	srand(seed);
	srand48(seed);

	/* initialize memory manager */
	mm = memmgr_init();

	/* initialize test */
	block_array_init();

	/* test allocators ! */
	if (threads > 1)
	{
		uint32_t i;

		pthread_t threadid[1024];

		for (i = 0; i < threads; i++) {
			pthread_create(&threadid[i], NULL, memmgr_test, NULL);
			fprintf(stderr, "Started thread $%.8x.\n", (uint32_t)threadid[i]);
		}

		for (i = 0; i < threads; i++) {
			pthread_join(threadid[i], NULL);
			fprintf(stderr, "Finished thread $%.8x.\n", (uint32_t)threadid[i]);
		}
	} else {
		memmgr_test(NULL);
	}

	memmgr_print(mm);

	return 0;
}
