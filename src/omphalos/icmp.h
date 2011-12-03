#ifndef OMPHALOS_ICMP
#define OMPHALOS_ICMP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef IPPROTO_ICMP6
#define IPPROTO_ICMP6	58
#endif

struct interface;
struct omphalos_packet;

void handle_icmp_packet(struct omphalos_packet *,const void *,size_t);

void handle_icmp6_packet(struct omphalos_packet *,const void *,size_t);

int tx_broadcast_pings(int,struct interface *,const void *);

#ifdef __cplusplus
}
#endif

#endif
