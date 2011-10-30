#ifndef OMPHALOS_IETF
#define OMPHALOS_IETF

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>

const wchar_t *ietf_multicast_lookup(int,const void *);
const wchar_t *ietf_bcast_lookup(int,const void *);
const wchar_t *ietf_local_lookup(int,const void *);

#ifdef __cplusplus
}
#endif

#endif
