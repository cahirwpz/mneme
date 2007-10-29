#ifndef __MEMMAN_AO_H
#define __MEMMAN_AO_H

#include "common.h"
#include "areamgr.h"

/* function prototypes */
void mm_init(areamgr_t *mm);
void *mm_alloc(areamgr_t *mm, uint32_t size, uint32_t alignment);
bool mm_realloc(areamgr_t *mm, void *memory, uint32_t new_size);
void mm_free(areamgr_t *mm, void *memory);
void mm_print(areamgr_t *mm);

#endif
