#ifndef OMPHALOS_UDP
#define OMPHALOS_UDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_iface;

void handle_udp_packet(const struct omphalos_iface *,struct interface *,
					const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
