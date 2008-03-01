#ifndef __EQSBMGR_H
#define __EQSBMGR_H

#include "common.h"
#include "areamgr.h"

#define AREA_MGR_EQSBMGR 1

/* Manager structure */

struct eqsbmgr
{
	arealst_t arealst;

	areamgr_t *areamgr;
};

typedef struct eqsbmgr eqsbmgr_t;

/* function prototypes */
void eqsbmgr_init(eqsbmgr_t *self, areamgr_t *areamgr);
void *eqsbmgr_alloc(eqsbmgr_t *self, uint32_t size, uint32_t alignment);
bool eqsbmgr_realloc(eqsbmgr_t *self, void *memory, uint32_t new_size);
bool eqsbmgr_free(eqsbmgr_t *self, void *memory);
bool eqsbmgr_verify(eqsbmgr_t *self, bool verbose);

#endif
