#ifndef OMPHALOS_DHCP
#define OMPHALOS_DHCP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

int handle_dhcp_packet(struct omphalos_packet *,const void *,size_t) __attribute__ ((nonnull (1,2)));
int handle_dhcp6_packet(struct omphalos_packet *,const void *,size_t) __attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
