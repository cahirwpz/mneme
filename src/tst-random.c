/*
 * Random malloc / free test.
 */

#include "memman-ao.h"
#include <stdio.h>

#define MAX_BLOCK_NUM	(1 << 16)
#define MAX_MEM_USED	(1 << 20)
#define MAX_BLOCK_SIZE	(1 << 14)

#define MM_PRINT_AT_ITERATION 1

static struct
{
	void		*array[MAX_BLOCK_NUM];
	uint32_t	last;
} blocks;

static memarea_t mm;

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

	mm_init(&mm);

	for (i = 0; i < ops; )
	{
		int32_t op = rand();

		if (op <= (RAND_MAX >> 1)) {
			/* If to many block don't allocate new one */
			if (blocks.last == MAX_BLOCK_NUM)
				continue;

			int32_t s = rand();

			s = (s * s) & (MAX_BLOCK_SIZE - 1);

			if (s < 4)
				s = 4;

			blocks.array[blocks.last + 1] = mm_alloc(&mm, s);

			/* if couldn't allocate then give up */
			if (blocks.array[blocks.last + 1] == NULL)
				continue;

			fprintf(stderr, "malloc(%d) = %p\n", s, blocks.array[++blocks.last]);
		} else {
			if (blocks.last == -1)
				continue;

			uint32_t i = rand() % (blocks.last + 1);

			mm_free(&mm, blocks.array[i]);

			fprintf(stderr, "free(%p)\n", blocks.array[i]);

			if (blocks.last != i)
				blocks.array[i] = blocks.array[blocks.last];

			blocks.array[blocks.last--] = NULL;
		}

#if MM_PRINT_AT_ITERATION == 1
		 mm_print(&mm);
#endif
		i++;
	}
	
#if MM_PRINT_AT_ITERATION == 0
	mm_print(&mm);
#endif

	return 0;
}
