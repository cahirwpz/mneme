#ifndef __BLKMGR_H
#define __BLKMGR_H

#include "common.h"
#include "areamgr.h"

#define AREA_MGR_BLKMGR	2

struct blkmgr
{
	arealst_t blklst;

	areamgr_t *areamgr;
};

typedef struct blkmgr blkmgr_t;

/* function prototypes */
void blkmgr_init(blkmgr_t *blkmgr, areamgr_t *areamgr);
void *blkmgr_alloc(blkmgr_t *blkmgr, uint32_t size, uint32_t alignment);
bool blkmgr_realloc(blkmgr_t *blkmgr, void *memory, uint32_t new_size);
bool blkmgr_free(blkmgr_t *blkmgr, void *memory);
bool blkmgr_verify(blkmgr_t *blkmgr, bool verbose);

#endif
