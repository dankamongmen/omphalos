#ifndef OMPHALOS_LLTD
#define OMPHALOS_LLTD

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct interface;
struct omphalos_packet;

void handle_lltd_packet(struct omphalos_packet *,const void *,size_t);

int initiate_lltd(int,struct interface *,const void *);

#ifdef __cplusplus
}
#endif

#endif
