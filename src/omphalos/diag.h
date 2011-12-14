#ifndef OMPHALOS_DIAG
#define OMPHALOS_DIAG

#ifdef __cplusplus
extern "C" {
#endif

// Uses the omphalos_ctx's ->diag function pointer. Acquires omphalos_ctx via
// lookup on a TSD (omphalos_ctx_key).
void diagnostic(const char *,...) __attribute__ ((format (printf,1,2)));

// Get the last n diagnostics. If n is 0 or larger than the number of available
// diagnostics, all available diagnostics will be returned. Each diagnostic
// will be terminated with the provided separator.
char *get_logs(unsigned,int);

#ifdef __cplusplus
}
#endif

#endif
