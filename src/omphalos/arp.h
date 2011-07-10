#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;
struct omphalos_packet;

void handle_arp_packet(const struct omphalos_iface *,struct omphalos_packet *,
				const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
