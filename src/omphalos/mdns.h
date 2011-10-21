#ifndef OMPHALOS_MDNS
#define OMPHALOS_MDNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct l2host;
struct l3host;
struct interface;
struct omphalos_iface;
struct omphalos_packet;

void handle_mdns_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t) __attribute__ ((nonnull (1,2,3)));

int tx_mdns_ptr(const struct omphalos_iface *,struct interface *,const char *,
			int,const void *) __attribute__ ((nonnull (1,2,3,5)));

#ifdef __cplusplus
}
#endif

#endif
