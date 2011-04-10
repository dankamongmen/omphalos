#ifndef OMPHALOS_OMPHALOS
#define OMPHALOS_OMPHALOS

#ifdef __cplusplus
extern "C" {
#endif

// Process-scope settings, generally configured on startup based off
// command-line options.
typedef struct omphalos_ctx {
	const char *pcapfn;
	unsigned long count;
	const char *user;
} omphalos_ctx;

#ifdef __cplusplus
}
#endif

#endif
