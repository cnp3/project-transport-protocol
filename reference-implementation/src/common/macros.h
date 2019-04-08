#ifndef __MACROS_H_
#define __MACROS_H_

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define _LOG(prefix, msg, ...)\
	do {\
		fprintf(stderr, prefix msg "\n", ##__VA_ARGS__);\
	} while(0)


#define LOG(msg, ...) _LOG("", msg, ##__VA_ARGS__)
#define ERROR(msg, ...) _LOG("[ERROR] ", msg, ##__VA_ARGS__)


#define _trace(x, lvl, ...) lvl("%s.%d: " x, __FILE__, __LINE__, ##__VA_ARGS__)
#define trace(x, ...) _trace(x, LOG, ##__VA_ARGS__)
#define trace_error(x, ...) _trace(x, ERROR, ##__VA_ARGS__)

#define goto_trace(label, x, ...) \
    do { \
        trace_error(x, ##__VA_ARGS__); \
        goto label; \
    } while (0)

#define goto_errno(label) goto_trace(label, "%s", strerror(errno))

#define ASSERT_ALWAYS(x, msg, ...) do {\
	if (!(x)) {\
		trace_error(msg, ##__VA_ARGS__);\
		exit(EXIT_FAILURE);\
	}\
} while(0)

#ifdef _DEBUG
#define ASSERT(x, msg, ...) ASSERT_ALWAYS(x, msg, ##__VA_ARGS__)
#define DEBUG(x, ...) _LOG("[DEBUG] ", x, ##__VA_ARGS__)
#define PRIVATE
#else
#define ASSERT(x, msg, ...)
#define DEBUG(x, ...)
#define PRIVATE static
#endif
#define PUBLIC

#define NOTNULL(x) ASSERT((x) != NULL, #x " is NULL!")

#define PRECONDITION(cond, err) if (!(cond)) return err
#define VALIDIF(cond, err, msg, ...) if (!(cond)) {\
	ERROR("%s " msg, pkt_err_code(err), ##__VA_ARGS__);\
	return err;\
}

#endif
