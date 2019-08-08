#ifndef OMPHALOS_SOCKETCAN
#define OMPHALOS_SOCKETCAN

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

void handle_can_packet(struct omphalos_packet *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
