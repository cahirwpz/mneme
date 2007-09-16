#ifndef __MEMMAN_AO_H
#define __MEMMAN_AO_H

#include "common.h"
#include "areaman.h"

/* function prototypes */
void mm_init(memarea_t *mm);
void *mm_alloc(memarea_t *mm, uint32_t size, uint32_t alignment);
bool mm_realloc(memarea_t *mm, void *memory, uint32_t new_size);
void mm_free(memarea_t *mm, void *memory);
void mm_print(memarea_t *mm);

#endif
