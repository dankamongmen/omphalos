#ifndef OMPHALOS_ETHERNET
#define OMPHALOS_ETHERNET

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct timeval;
struct interface;

void handle_ethernet_packet(struct interface *,const struct timeval *,
				const void *,size_t,const unsigned char *);

#ifdef __cplusplus
}
#endif

#endif
