#ifndef OMPHALOS_ND
#define OMPHALOS_ND

#ifdef __cplusplus
extern "C" {
#endif

// IPv6 Neigbor Discovery (RFC 4861)

#include <stdint.h>
#include <omphalos/128.h>

struct omphalos_packet;

void handle_nd_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
