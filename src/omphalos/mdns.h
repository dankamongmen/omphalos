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

int tx_mdns_ptr(const struct omphalos_iface *,struct interface *,int,
		const char *) __attribute__ ((nonnull (1,2,4)));

#ifdef __cplusplus
}
#endif

#endif
