#ifndef OMPHALOS_DNS
#define OMPHALOS_DNS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;
struct omphalos_packet;

void handle_dns_packet(const struct omphalos_iface *,struct omphalos_packet *,
			const void *,size_t) __attribute__ ((nonnull (1,2,3)));

void tx_dns_a(const struct omphalos_iface *,int,const void *,const char *)
		__attribute__ ((nonnull (1,3,4)));
void tx_dns_aaaa(const struct omphalos_iface *,int,const void *,const char *)
		__attribute__ ((nonnull (1,3,4)));

#ifdef __cplusplus
}
#endif

#endif
