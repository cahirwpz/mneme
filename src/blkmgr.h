#ifndef __BLKMGR_H
#define __BLKMGR_H

#include "common.h"
#include "areamgr.h"

struct blkmgr
{
	areamgr_t local;
	areamgr_t *global;
};

typedef struct blkmgr blkmgr_t;

/* function prototypes */
void blkmgr_init(blkmgr_t *blkmgr, areamgr_t *areamgr);
void *blkmgr_alloc(blkmgr_t *blkmgr, uint32_t size, uint32_t alignment);
bool blkmgr_realloc(blkmgr_t *blkmgr, void *memory, uint32_t new_size);
void blkmgr_free(blkmgr_t *blkmgr, void *memory);
void blkmgr_print(blkmgr_t *blkmgr);

#endif
