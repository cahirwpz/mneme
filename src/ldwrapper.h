#ifndef __LDWRAPPER_H
#define __LDWRAPPER_H

#include "common.h"

struct mallinfo {
	size_t arena;    /* non-mmapped space allocated from system */
	size_t ordblks;  /* number of free chunks */
	size_t smblks;   /* always 0 */
	size_t hblks;    /* always 0 */
	size_t hblkhd;   /* space in mmapped regions */
	size_t usmblks;  /* maximum total allocated space */
	size_t fsmblks;  /* always 0 */
	size_t uordblks; /* total allocated space */
	size_t fordblks; /* total free space */
	size_t keepcost; /* releasable (via malloc_trim) space */
};

extern void (*__free_hook) (void *PTR, const void *CALLER);
extern void *(*__malloc_hook) (size_t SIZE, const void *CALLER);
extern void *(*__realloc_hook) (void *PTR, size_t SIZE, const void *CALLER);
extern void *(*__memalign_hook)(size_t SIZE, size_t ALIGNMENT, const void *CALLER);

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void cfree(void *ptr);
void *realloc(void *ptr, size_t size);
void *memalign(size_t boundary, size_t size);
void *valloc(size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
int mallopt(int param, int value);
struct mallinfo mallinfo(void);
int memcheck(void (*ABORTFN)(void));

#endif
