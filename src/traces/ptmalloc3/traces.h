#ifndef __TRACES_H
#define __TRACES_H

#include <stdint.h>
#include <time.h>

#define OP_FREE			0
#define OP_MALLOC		1
#define OP_REALLOC		2
#define OP_MEMALIGN		3

/* log format */

struct traces_log
{
	/* miliseconds till start of the process execution */
	uint32_t msec;

	/* operation code & flags */
	uint16_t opcode;

	/* process/thread identificators */
	uint16_t pid;
	uint32_t thrid;

	/* result */
	uint32_t result;

	/* arguments */
	uint32_t args[2];
};

typedef struct traces_log traces_log_t;

/* functions */

traces_log_t *traces_prologue(void);
void traces_epilogue_free(traces_log_t *logline, void *ptr);
void *traces_epilogue_malloc(traces_log_t *logline, size_t size, void *result);
void *traces_epilogue_realloc(traces_log_t *logline, void *ptr, size_t size, void *result);
void *traces_epilogue_memalign(traces_log_t *logline, size_t alignment, size_t size, void *result);
void traces_init_hook(void);

#endif
