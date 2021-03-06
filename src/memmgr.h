#ifndef __MEMMGR_H
#define __MEMMGR_H

#include "common.h"
#include "areamgr.h"
#include "eqsbmgr.h"
#include "blkmgr.h"
#include "mmapmgr.h"

/* */

struct percpumgr {
	eqsbmgr_t  eqsbmgr;
	blkmgr_t  blkmgr;
	mmapmgr_t mmapmgr;
};

typedef struct percpumgr percpumgr_t;

/* */

struct memmgr {
	areamgr_t areamgr;

	percpumgr_t percpumgr[0];
};

typedef struct memmgr memmgr_t;

/* function prototypes */
memmgr_t *memmgr_init();
void *memmgr_alloc(memmgr_t *memmgr, uint32_t size, uint32_t alignment);
bool memmgr_realloc(memmgr_t *memmgr, void *memory, uint32_t new_size);
bool memmgr_free(memmgr_t *memmgr, void *memory);
void memmgr_verify(memmgr_t *memmgr, bool verbose);

#endif
