#ifndef OMPHALOS_DIAG
#define OMPHALOS_DIAG

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>

// Uses the omphalos_ctx's ->diag function pointer. Acquires omphalos_ctx via
// lookup on a TSD (omphalos_ctx_key).
void diagnostic(const wchar_t *,...);
// FIXME __attribute__ ((format (printf,1,2)));

#ifdef __cplusplus
}
#endif

#endif
