/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Random malloc / free test
 */

#include "memmgr.h"
#include <stdio.h>

#define MAX_BLOCK_NUM		(1 << 16)
#define MAX_MEM_USED		(1 << 20)
#define MAX_BLOCK_SIZE		(1 << 5)
#define MAX_REALLOC_SIZE	(1 << 4)
#define MAX_ALIGN_BITS		0

#define MM_PRINT_AT_ITERATION 1

struct block
{
	void    *ptr;
	int32_t size;
};

typedef struct block block_t;

static struct
{
	block_t array[MAX_BLOCK_NUM];
	int32_t last;
} blocks;

static memmgr_t *mm;

void usage(char *progname)
{
	printf("Usage: %s seed opsnum\n"
		   "\n"
		   "  seed   - seed for pseudo random number generator\n"
		   "  opsnum - number of malloc / free operations (at least 100)\n"
		   "\n", progname);

	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	if (argc != 3)
		usage(argv[0]);

	int32_t seed;
	{
		char *tmp;

		seed = strtol(argv[1], &tmp, 10);

		if (!(*argv[1] != '\0' && *tmp == '\0'))
			usage(argv[0]);
	}

	int32_t ops;
	{
		char *tmp;

		ops = strtol(argv[2], &tmp, 10);

		if (!(*argv[1] != '\0' && *tmp == '\0'))
			usage(argv[0]);

		if (ops < 100)
			usage(argv[0]);
	}

	int32_t i;

	blocks.last = -1;

	srand(seed);

	mm = memmgr_init();

	for (i = 0; i < ops; )
	{
		uint32_t op = rand();

#if VERBOSE == 1
		memmgr_print(mm);
#endif

		if (op <= 2 * (RAND_MAX >> 4)) {
			/* case for mm_realloc */
			continue;

			if (blocks.last == -1)
				continue;

			int32_t i = rand() % (blocks.last + 1);
			int32_t s = rand();

			s = (s & (MAX_REALLOC_SIZE - 1)) - (MAX_REALLOC_SIZE >> 1);
			
			s += blocks.array[i].size;

			if (s < 4)
				s = 4;

			if (memmgr_realloc(mm, blocks.array[i].ptr, s))
				blocks.array[blocks.last + 1].size = s;
		} else if (op <= 3 * (RAND_MAX >> 4)) {
			/* case for mm_alloc_aligned */
			if (blocks.last == MAX_BLOCK_NUM)
				continue;

			int32_t s = rand();

			s = s & (MAX_BLOCK_SIZE - 1);

			if (s < 4)
				s = 4;

			uint32_t alignment = 1 << (rand() % (MAX_ALIGN_BITS + 1));

			if (alignment < 16)
				alignment = 16;

			blocks.array[blocks.last + 1].ptr  = memmgr_alloc(mm, s, alignment);
			blocks.array[blocks.last + 1].size = s;

			/* if couldn't allocate then give up */
			if (blocks.array[blocks.last + 1].ptr == NULL) {
				DEBUG("memmgr_alloc aligned: out of memory!\n");
				abort();
			}
				continue;

			DEBUG("memalign(%d, %d) = %p\n", s, alignment, blocks.array[blocks.last + 1].ptr);

			blocks.last++;
		} else if (op <= 8 * (RAND_MAX >> 4)) {
			/* case for memmgr_alloc */
			if (blocks.last == MAX_BLOCK_NUM)
				continue;

			int32_t s = rand();

			s = s & (MAX_BLOCK_SIZE - 1);

			if (s < 4)
				s = 4;

			blocks.array[blocks.last + 1].ptr  = memmgr_alloc(mm, s, 0);
			blocks.array[blocks.last + 1].size = s;

			/* if couldn't allocate then give up */
			if (blocks.array[blocks.last + 1].ptr == NULL) {
				DEBUG("memmgr_alloc: out of memory!\n");
				abort();
			}

			DEBUG("malloc(%d) = %p\n", s, blocks.array[blocks.last + 1].ptr);

			blocks.last++;
		} else {
			/* case for memmgr_free */
			if (blocks.last == -1)
				continue;

			int32_t i = rand() % (blocks.last + 1);

			DEBUG("free(%p, %u)\n", blocks.array[i].ptr, blocks.array[i].size);

			if (!memmgr_free(mm, blocks.array[i].ptr)) {
				DEBUG("memmgr_free: could not free block!\n");
				abort();
			}

			if (blocks.last != i)
				blocks.array[i] = blocks.array[blocks.last];

			blocks.array[blocks.last].ptr  = NULL;
			blocks.array[blocks.last].size = 0;

			blocks.last--;
		}

#if MM_PRINT_AT_ITERATION == 1
		 memmgr_print(mm);
#endif
		i++;
	}
	
#if MM_PRINT_AT_ITERATION == 0
	memmgr_print(mm);
#endif

	return 0;
}
