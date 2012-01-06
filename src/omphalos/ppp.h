#ifndef OMPHALOS_PPP
#define OMPHALOS_PPP

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_packet;

void handle_ppp_packet(struct omphalos_packet *,const void *,size_t)
			__attribute__ ((nonnull (1,2)));

#ifdef __cplusplus
}
#endif

#endif
