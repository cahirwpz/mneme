/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	
 */

#undef VERBOSE
#define VERBOSE 0

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>

#include "memmgr.h"
#include "ldwrapper.h"

void (*__free_hook) (void *PTR, const void *CALLER) = NULL;
void *(*__malloc_hook) (size_t SIZE, const void *CALLER) = NULL;
void *(*__realloc_hook) (void *PTR, size_t SIZE, const void *CALLER) = NULL;
void *(*__memalign_hook)(size_t SIZE, size_t ALIGNMENT, const void *CALLER) = NULL;

static bool ma_initialized = FALSE;
static struct memmgr *mm;
static sem_t ma_sem;

static void ldwrapper_exit()
{
	sem_destroy(&ma_sem);
}

static void ldwrapper_init() {
	if (__sync_bool_compare_and_swap(&ma_initialized, FALSE, TRUE)) {
		DEBUG("initialize subsystem\n");

		sem_init(&ma_sem, 0, 1);

		atexit(&ldwrapper_exit);

		memmgr_init(mm);
	}

	while (__sync_and_and_fetch(&ma_initialized, TRUE) == FALSE) {
		DEBUG("waiting for initialization\n");
	}
}

void *malloc(size_t size)
{
	ldwrapper_init();

	sem_wait(&ma_sem);

	void *area = memmgr_alloc(mm, size, 0);

	sem_post(&ma_sem);

	fprintf(stderr, "alloc(%u) = %p\n", size, area);

	return area;
}

void *calloc(size_t nmemb, size_t size)
{
	void *area = malloc(size * nmemb);
	
	if (area != NULL) {
		memset(area, 0, size * nmemb);
	}

	return area;
}

void free(void *ptr)
{
	assert(ma_initialized == TRUE);

	if (ptr != NULL) {
		sem_wait(&ma_sem);

		memmgr_free(mm, ptr);

		sem_post(&ma_sem);
	}

	fprintf(stderr, "free(%p)\n", ptr);
}

void cfree(void *ptr)
{
	free(ptr);
}

void *realloc(void *ptr, size_t size)
{
	assert(ma_initialized == TRUE);

	if (ptr == NULL)
		return malloc(size);

	if (size == 0) {
		free(ptr);
		
		return NULL;
	}

	sem_wait(&ma_sem);

	bool res = memmgr_realloc(mm, ptr, size);

	sem_post(&ma_sem);

	void *newptr = ptr;

	if (!res) {
		newptr = malloc(size);

		memcpy(newptr, ptr, sizeof(size));

		free(ptr);
	}

	fprintf(stderr, "realloc(%p, %u) = %p\n", ptr, size, newptr);

	return newptr;
}

void *memalign(size_t boundary, size_t size)
{
	ldwrapper_init();

	sem_wait(&ma_sem);

	void *area = memmgr_alloc(mm, size, boundary);

	sem_post(&ma_sem);

	fprintf(stderr, "memalign(%u, %u) = %p\n", boundary, size, area);

	return area;
}

void *valloc(size_t size)
{
	return memalign(sysconf(_SC_PAGESIZE),size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *area = memalign(alignment, size);

	*memptr = area;

	return area ? 0 : -ENOMEM;
}

int mallopt(int param, int value)
{
	fprintf(stderr, "mallopt: not implemented!\n");

	return -1;
}

struct mallinfo mallinfo(void)
{
	fprintf(stderr, "mallinfo: not implemented!\n");

	struct mallinfo empty;

	memset((void *)&empty, 0, sizeof(struct mallinfo));

	return empty;
}
