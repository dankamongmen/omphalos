#ifndef OMPHALOS_TX
#define OMPHALOS_TX

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

struct interface;

// Acquire a frame from the ringbuffer. Interface lock must be held.
void *get_tx_frame(struct interface *,size_t *);

// Mark a frame as ready-to-send. Interface lock must be held.
int send_tx_frame(struct interface *,void *);

// Release a frame for reuse without transmitting it. Interface lock must be held.
void abort_tx_frame(struct interface *,void *);

// Frame preparation

// ------------ ARP ------------

// ARP request. Sent to the broadcast link address for the interface,
// requesting information on the network address from any host.
//void prepare_arp_req(const struct omphalos_iface *,const struct interface *,
//			void *,size_t *,const void *,size_t);

// ARP probe. Sent to the specified link address.
void prepare_arp_probe(const struct interface *,void *,size_t *,const void *,
			size_t,const uint32_t *,const uint32_t *);

#ifdef __cplusplus
}
#endif

#endif
