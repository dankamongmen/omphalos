#ifndef OMPHALOS_MDNS
#define OMPHALOS_MDNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;
struct omphalos_packet;

void handle_mdns_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t) __attribute__ ((nonnull (1,2,3)));

#ifdef __cplusplus
}
#endif

#endif
