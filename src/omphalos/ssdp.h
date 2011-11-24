#ifndef OMPHALOS_SSDP
#define OMPHALOS_SSDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

// Returns 1 for a valid SSDP response, -1 for a valid SSDP query, 0 otherwise
int handle_ssdp_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
