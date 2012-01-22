#ifndef OMPHALOS_DIAG
#define OMPHALOS_DIAG

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

// Uses the omphalos_ctx's ->diag function pointer. Acquires omphalos_ctx via
// lookup on a TSD (omphalos_ctx_key).
void diagnostic(const char *,...) __attribute__ ((format (printf,1,2)));

typedef struct logent {
	char *msg;
	time_t when;
} logent;

#define MAXIMUM_LOG_ENTRIES 1024

// Get up to the last n diagnostics. n should not be 0 mor greater than
// MAXIMUM_LOG_ENTRIES. If there are less than n present, they'll be copied
// into the first n logents; logent[n].msg will then be NULL.
int get_logs(unsigned,logent *);

#ifdef __cplusplus
}
#endif

#endif
