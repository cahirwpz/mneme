#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>

#include "memman-ao.h"

static bool ma_initialized = FALSE;
static struct memarea mm;
static sem_t ma_sem;

static void ldwrapper_exit()
{
	sem_destroy(&ma_sem);
}

void *malloc(size_t size)
{
	if (__sync_bool_compare_and_swap(&ma_initialized, FALSE, TRUE)) {
		DEBUG("initialize subsystem\n");

		sem_init(&ma_sem, 0, 1);

		atexit(&ldwrapper_exit);

		mm_init(&mm);
	}

	while (__sync_and_and_fetch(&ma_initialized, TRUE) == FALSE) {
		DEBUG("waiting for initialization\n");
	}

	sem_wait(&ma_sem);

	void *area = mm_alloc(&mm, size);

	sem_post(&ma_sem);

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

		mm_free(&mm, ptr);

		sem_post(&ma_sem);
	}
}

void cfree(void *ptr)
{
	free(ptr);
}

void *realloc(void *ptr, size_t size)
{
	return NULL;
}

void *memalign(size_t boundary, size_t size)
{
	DEBUG("not implemented!\n");

	return NULL;
}

void *valloc(size_t size)
{
	return memalign(getpagesize(), size);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	DEBUG("not implemented!\n");

	*memptr = NULL;

	return 0;
}

int mallopt(int param, int value)
{
	DEBUG("not implemented!\n");

	return -1;
}
