#ifndef OMPHALOS_UDP
#define OMPHALOS_UDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct omphalos_iface;
struct omphalos_packet;

// These refer to source ports, since we're looking for responses
#define DNS_UDP_PORT 53
#define DHCP_UDP_PORT 57
#define SSDP_UDP_PORT 1900
#define MDNS_UDP_PORT 5353

void handle_udp_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);

#define MINPORT 4097
static inline unsigned
random_udp_port(void){
	return (random() % (0xffff - MINPORT)) + MINPORT;
}
#undef MINPORT

#ifdef __cplusplus
}
#endif

#endif
