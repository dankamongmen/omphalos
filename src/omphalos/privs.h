#ifndef OMPHALOS_PRIVS
#define OMPHALOS_PRIVS

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/capability.h>

int handle_priv_drop(const char *,const cap_value_t *,unsigned);

#ifdef __cplusplus
}
#endif

#endif
