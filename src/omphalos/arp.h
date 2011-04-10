#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

void handle_arp_packet(struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
