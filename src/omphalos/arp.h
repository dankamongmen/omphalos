#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <omphalos/interface.h>

struct omphalos_packet;

void handle_arp_packet(struct omphalos_packet *,const void *,size_t);

void send_arp_req(struct interface *,const void *,const uint32_t *,const uint32_t *);

static inline void
send_arp_probe(interface *i,const uint32_t *addr){
	uint32_t saddr = 0ul;

	if(i->bcast){
		send_arp_req(i,i->bcast,addr,&saddr);
	}
}

#ifdef __cplusplus
}
#endif

#endif
