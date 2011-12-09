#ifndef OMPHALOS_MPLS
#define OMPHALOS_MPLS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

void handle_mpls_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
