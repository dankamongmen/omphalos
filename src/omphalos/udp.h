#ifndef OMPHALOS_UDP
#define OMPHALOS_UDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct omphalos_iface;
struct omphalos_packet;

#define DNS_UDP_PORT 53
#define MDNS_UDP_PORT 5353

void handle_udp_packet(const struct omphalos_iface *,struct omphalos_packet *,
					const void *,size_t);

uint16_t udp4_csum(const void *hdr) __attribute__ ((nonnull (1)));
uint16_t udp6_csum(const void *hdr) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
