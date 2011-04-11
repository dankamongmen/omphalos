#ifndef OMPHALOS_IP
#define OMPHALOS_IP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

// Ethernet protocols 0x8000 and 0x86DD
void handle_ipv4_packet(struct interface *,const void *,size_t);
void handle_ipv6_packet(struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
