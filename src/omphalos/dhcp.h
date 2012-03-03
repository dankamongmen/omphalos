#ifndef OMPHALOS_DHCP
#define OMPHALOS_DHCP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <omphalos/128.h>

struct interface;
struct omphalos_packet;

int handle_dhcp_packet(struct omphalos_packet *,const void *,size_t) __attribute__ ((nonnull (1,2)));
int handle_dhcp6_packet(struct omphalos_packet *,const void *,size_t) __attribute__ ((nonnull (1,2)));

int dhcp4_probe(struct interface *,const uint32_t *);
int dhcp6_probe(struct interface *,const uint128_t);

#ifdef __cplusplus
}
#endif

#endif
