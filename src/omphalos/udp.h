#ifndef OMPHALOS_UDP
#define OMPHALOS_UDP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

void handle_udp_packet(struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
