#ifndef __MEMMAN_H
#define __MEMMAN_H

#include "common.h"
#include "areaman.h"

struct percpumgr {
};

struct memmgr {
	areamgr_t areamgr;
};

typedef struct memmgr memmgr_t;

/* function prototypes */
memman_t *mm_init();
void *mm_alloc(memman_t *mm, uint32_t size, uint32_t alignment);
bool mm_realloc(memman_t *mm, void *memory, uint32_t new_size);
void mm_free(memman_t *mm, void *memory);
void mm_print(memman_t *mm);

#endif

