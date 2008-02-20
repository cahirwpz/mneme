#ifndef __MMAPMGR_H
#define __MMAPMGR_H

#include "areamgr.h"
#include <pthread.h>

#define AREA_MGR_MMAPBLK 3

struct mmapmgr
{
	arealst_t blklst;

	areamgr_t *areamgr;
};

typedef struct mmapmgr mmapmgr_t;

/* */

void mmapmgr_init(mmapmgr_t *mmapmgr, areamgr_t *areamgr);
void *mmapmgr_alloc(mmapmgr_t *mmapmgr, uint32_t size, uint32_t alignment);
bool mmapmgr_realloc(mmapmgr_t *mmapmgr, void *memory, uint32_t new_size);
bool mmapmgr_free(mmapmgr_t *mmapmgr, void *memory);
void mmapmgr_print(mmapmgr_t *mmapmgr);

#endif
