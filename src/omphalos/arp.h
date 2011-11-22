#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct interface;
struct omphalos_packet;

void handle_arp_packet(struct omphalos_packet *,const void *,size_t);

void send_arp_probe(struct interface *,const void *,const uint32_t *,const uint32_t *);

#ifdef __cplusplus
}
#endif

#endif
