#ifndef OMPHALOS_LLTD
#define OMPHALOS_LLTD

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define ETH_P_LLTD	0x88d9	// Link Layer Topology Discovery Protocol

struct interface;
struct omphalos_packet;

int init_lltd_service(void);
int stop_lltd_service(void);

void handle_lltd_packet(struct omphalos_packet *,const void *,size_t);

int initiate_lltd(int,struct interface *,const void *);

#ifdef __cplusplus
}
#endif

#endif
