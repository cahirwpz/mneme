#include "memman.h"

/* */

typedef struct area_manager area_manager_t;

struct cpu_manager
{
	
} __attribute__((aligned(L2_LINE_SIZE)));

/* */

memman_t *mm_init()
{
	return NULL;
}

void *mm_alloc(memman_t *mm, uint32_t size, uint32_t alignment)
{
	return NULL;
}

bool mm_realloc(memman_t *mm, void *memory, uint32_t new_size)
{
	return FALSE;
}

void mm_free(memman_t *mm, void *memory)
{
}

void mm_print(memman_t *mm)
{
}
