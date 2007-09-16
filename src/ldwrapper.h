#ifndef __LDWRAPPER_H
#define __LDWRAPPER_H

#include "common.h"

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void cfree(void *ptr);
void *realloc(void *ptr, size_t size);
void *memalign(size_t boundary, size_t size);
void *valloc(size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
int mallopt(int param, int value);

#endif
