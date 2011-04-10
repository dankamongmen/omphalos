#ifndef OMPHALOS_IP
#define OMPHALOS_IP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;

void handle_ip_packet(struct interface *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
