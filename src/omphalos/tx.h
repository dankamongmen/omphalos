#ifndef OMPHALOS_TX
#define OMPHALOS_TX

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct interface;
struct omphalos_iface;

// Acquire a frame from the ringbuffer
void *get_tx_frame(const struct omphalos_iface *,struct interface *,size_t *);

// Mark a frame as ready-to-send
void send_tx_frame(const struct omphalos_iface *,struct interface *,void *);

// Frame preparation

// ARP
void prepare_arp_req(const struct omphalos_iface *,const struct interface *,
			void *,const void *,size_t);

#ifdef __cplusplus
}
#endif

#endif
