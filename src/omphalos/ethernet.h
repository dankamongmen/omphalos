#ifndef OMPHALOS_ETHERNET
#define OMPHALOS_ETHERNET

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void handle_ethernet_packet(const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
