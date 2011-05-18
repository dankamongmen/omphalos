#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_iface;

void handle_arp_packet(const struct omphalos_iface *,struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
