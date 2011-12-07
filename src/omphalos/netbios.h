#ifndef OMPHALOS_NETBIOS
#define OMPHALOS_NETBIOS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_packet;

#define NETBIOS_UDP_PORT 138

// Returns 1 for a valid NetBIOS response, -1 for a valid NetBIOS query, 0 otherwise
int handle_netbios_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
