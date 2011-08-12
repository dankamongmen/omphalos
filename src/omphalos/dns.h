#ifndef OMPHALOS_DNS
#define OMPHALOS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;
struct omphalos_packet;

void handle_dns_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
