#ifndef OMPHALOS_VRRP
#define OMPHALOS_VRRP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

// Virtual Router Redundancy Protocol
void handle_vrrp_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
