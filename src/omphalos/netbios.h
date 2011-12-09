#ifndef OMPHALOS_NETBIOS
#define OMPHALOS_NETBIOS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_packet;

#define NETBIOS_NS_UDP_PORT 137	// Name service
#define NETBIOS_DS_UDP_PORT 138 // Datagram service
#define NETBIOS_SS_TCP_PORT 139 // Session service

// Returns 1 for a valid NetBIOS-NS response, -1 for a valid NetBIOS-NS query,
// 0 otherwise (not valid NetBIOS-NS)
int handle_netbios_ns_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
