#ifndef OMPHALOS_ICMP
#define OMPHALOS_ICMP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <omphalos/128.h>

#ifndef IPPROTO_ICMP6
#define IPPROTO_ICMP6	58
#endif

struct interface;
struct omphalos_packet;

void handle_icmp_packet(struct omphalos_packet *,const void *,size_t);

void handle_icmp6_packet(struct omphalos_packet *,const void *,size_t);

int tx_ipv4_bcast_pings(struct interface *,const uint32_t *);
int tx_ipv6_bcast_pings(struct interface *,const uint128_t);

#ifdef __cplusplus
}
#endif

#endif
