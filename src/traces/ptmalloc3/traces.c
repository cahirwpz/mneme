#include <alloca.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic_ops.h>

#include "traces.h"

#define VERBOSE 0

#if VERBOSE == 1
#define DEBUG(format, args...) fprintf(stderr, "%s: " format "\n", __func__, ##args)
#else
#define DEBUG(format, args...)
#endif

/* traces data */
#define LAST_LOGLINE	4096

struct traces_data
{
	/* file descriptor of log file */
	int32_t	logfd;

	/* [0, LAST_LOGLINE-1]    => free log line
	   LAST_LOGLINE			  => locking trigger
	   [LAST_LOGLINE+1, +inf] => lock is acquired or being acquired */
	volatile AO_t logcnt;
	
	volatile AO_TS_t lock;

	/* how many lines are currently used ? */
	volatile AO_t usecnt;
	volatile AO_t lines;

	/* source of time */
	clockid_t clkid;

	/* log lines */
	traces_log_t logs[LAST_LOGLINE - 1];
};

typedef struct traces_data traces_data_t;

static traces_data_t *traces = NULL;

/**
 * 
 */

static inline int traces_obtain_log_line_number(void)
{
	int linenum;

	DEBUG("begin");

	while (1) {
		/* wait for buffer to be emptied */
		while (AO_load(&traces->logcnt) > LAST_LOGLINE);

		while (AO_test_and_set(&traces->lock) == AO_TS_SET);
		
		/* new line will be used! */
		AO_fetch_and_add1(&traces->usecnt);
		
		/* get log line number */
		linenum = AO_fetch_and_add1(&traces->logcnt);

		AO_CLEAR(&traces->lock);

		/* line number with valid properties has been acquired so leave */
		if (linenum <= LAST_LOGLINE)
			break;
		
		/* release non-existing line number */
		AO_fetch_and_sub1(&traces->usecnt);
	}
	
	DEBUG("linenum = %d", linenum);

	/* linenum is storing now a unique number n, such as 0 <= n <= LAST_LOGLINE */
	return linenum;
}

/**
 *
 */

static void traces_log_write_out(void)
{
	DEBUG("begin");
	
	/* disallow to obtain new lines */
	while (AO_test_and_set(&traces->lock) == AO_TS_SET);

	/* if any line is still used, wait for it to be released */
	while (AO_load(&traces->usecnt) > 0);

	int lines = (traces->logcnt >= LAST_LOGLINE) ? (LAST_LOGLINE - 1) : traces->logcnt;
	
	if (lines > 0) {
		if (write(traces->logfd, &traces->logs, lines * sizeof(traces_log_t)) == -1)
			perror("traces: writing out failed: ");
		else
			DEBUG("written out %d lines", lines);
	}
	
	memset(&traces->logs, 0, (LAST_LOGLINE - 1) * sizeof(traces_log_t));

	/* reset line log counter */
	AO_store(&traces->logcnt, 0);
	AO_CLEAR(&traces->lock);
	
	DEBUG("finished");
}

/**
 *
 */

static traces_log_t *traces_obtain_log_line(void)
{
	int linenum = traces_obtain_log_line_number();

	/* write out log lines */
	while (linenum >= LAST_LOGLINE) {
		AO_fetch_and_sub1(&traces->usecnt);
		
		DEBUG("writing out");
		traces_log_write_out();
		
		linenum = traces_obtain_log_line_number();
	}

	traces_log_t *logline = &traces->logs[linenum];

	memset(logline, 0, sizeof(traces_log_t));

	struct timespec timestamp;

	clock_gettime(traces->clkid, &timestamp);

	logline->msec  = timestamp.tv_sec * 1000 + timestamp.tv_nsec / 1000000;
	logline->pid   = getpid();
	logline->thrid = pthread_self();

	return logline;
}

/**
 *
 */

static void traces_release_log_line(traces_log_t *logline)
{
	AO_fetch_and_sub1(&traces->usecnt);
}

/**
 * Prologue procedure.
 */

traces_log_t *traces_prologue(void)
{
	if (traces == NULL)
		traces_init_hook();

	return traces_obtain_log_line();
}

/**
 * Epilogue procedure for malloc.
 * @param logline
 * @param ptr
 */

void traces_epilogue_free(traces_log_t *logline, void *ptr)
{
	if (traces == NULL)
		traces_init_hook();

	if (logline == NULL)
		abort();

	logline->opcode  = OP_FREE;
	logline->args[0] = (uint32_t)ptr;

	traces_release_log_line(logline);
}

/**
 * Epilogue procedure for malloc.
 * @param logline
 * @param size
 * @param result
 * @return
 */

void *traces_epilogue_malloc(traces_log_t *logline, size_t size, void *result)
{
	if (traces == NULL)
		traces_init_hook();

	if (logline == NULL)
		abort();

	logline->opcode  = OP_MALLOC;
	logline->result  = (uint32_t)result;
	logline->args[0] = size;

	traces_release_log_line(logline);

	return result;
}

/**
 * Epilogue procedure for realloc.
 * @param logline
 * @param ptr
 * @param size
 * @param result
 * @return
 */

void *traces_epilogue_realloc(traces_log_t *logline, void *ptr, size_t size, void *result)
{
	if (traces == NULL)
		traces_init_hook();

	if (logline == NULL)
		abort();

	if (ptr == NULL) {
		logline->opcode  = OP_MALLOC;
		logline->result  = (uint32_t)result;
		logline->args[0] = size;
	} else if (size == 0) {
		logline->opcode  = OP_FREE;
		logline->args[0] = (uint32_t)ptr;
	} else {
		logline->opcode  = OP_REALLOC;
		logline->result  = (uint32_t)result;
		logline->args[0] = (uint32_t)ptr;
		logline->args[1] = size;
	}

	traces_release_log_line(logline);

	return result;
}

/**
 * Epilogue procedure for memalign.
 * @param logline
 * @param alignment
 * @param size
 * @param result
 * @return
 */

void *traces_epilogue_memalign(traces_log_t *logline, size_t alignment, size_t size, void *result)
{
	if (traces == NULL)
		traces_init_hook();

	if (logline == NULL)
		abort();

	logline->opcode	 = OP_MEMALIGN;
	logline->result	 = (uint32_t)result;
	logline->args[0] = alignment;
	logline->args[1] = size;

	traces_release_log_line(logline);

	return result;
}

/**
 * At exit procedure. Will write out log buffer.
 */

static void traces_at_exit(void)
{
	if (traces != NULL) {
		DEBUG("writing out");
		traces_log_write_out();
	}
}

/**
 *
 */

static AO_TS_t initlock = AO_TS_INITIALIZER;
static AO_t    initcnt  = 0;

void traces_init_hook(void)
{
	DEBUG("begin");
		
	while (AO_test_and_set(&initlock) == AO_TS_SET);
		
	if (traces == NULL) {
		DEBUG("inializing");
		
		/* create shared memory block for malloc traces */
		traces_data_t *_traces;
		
		_traces = mmap(NULL, sizeof(traces_log_t) * 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

		if (_traces == NULL) {
			perror("Cannot create shared memory segment for malloc traces:");
			abort();
		}
		
		DEBUG("traces @ $%.8x", (uint32_t)_traces);

		memset(&_traces->logs, 0, (LAST_LOGLINE - 1) * sizeof(traces_log_t));

		/* open log file */
		char *logname = getenv("MALLOC_TRACE_LOG");

		if (logname == NULL)
			logname = "trace-log.bin";
		
		DEBUG("traces logname = %s", logname);

		_traces->logfd  = open(logname, O_WRONLY|O_APPEND|O_CREAT, 0600);
		_traces->logcnt = 0;
		_traces->usecnt = 0;
		_traces->lock   = AO_TS_INITIALIZER;

		if (_traces->logfd == -1) {
			perror("Cannot open log file:");
			abort();
		}

		/* get clock id of the process */
		if (clock_getcpuclockid(getpid(), &_traces->clkid) != 0) {
			perror("Cannot obtain clock id:");
			abort();
		}

		traces = _traces;

		DEBUG("inialized");
	} else {
		DEBUG("already inialized");
	}
	
	AO_CLEAR(&initlock);
	
	/* atexit uses internally malloc ! */
	if ((AO_load(&initcnt) == 0) && (AO_fetch_and_add1(&initcnt) == 0))
		atexit(&traces_at_exit);
}
