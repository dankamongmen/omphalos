#ifndef OMPHALOS_UDP
#define OMPHALOS_UDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct omphalos_packet;

// These refer to source ports, since we're looking for responses
#define DNS_UDP_PORT 53
#define DHCP_UDP_PORT 67
#define BOOTP_UDP_PORT 68
#define SSDP_UDP_PORT 1900
#define MDNS_UDP_PORT 5353
#define MDNS_NATPMP1_UDP_PORT 5350
#define MDNS_NATPMP2_UDP_PORT 5351
#define DHCP6SRV_UDP_PORT 547
#define DHCP6CLI_UDP_PORT 546

void handle_udp_packet(struct omphalos_packet *,const void *,size_t);

#define MINPORT 4097
static inline unsigned
random_udp_port(void){
	return (random() % (0xffff - MINPORT)) + MINPORT;
}
#undef MINPORT

// Source and destination ports are in network byte order, and in the lowest
// 16 bits of the unsigned word
int prep_udp4(void *,size_t,unsigned,unsigned,size_t);
int prep_udp6(void *,size_t,unsigned,unsigned,size_t);

#ifdef __cplusplus
}
#endif

#endif
