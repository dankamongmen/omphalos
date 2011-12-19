#ifndef OMPHALOS_ARP
#define OMPHALOS_ARP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct interface;
struct omphalos_packet;

void handle_arp_packet(struct omphalos_packet *,const void *,size_t);

void send_arp_req(struct interface *,const void *,const uint32_t *,const uint32_t *);

static inline void
send_arp_probe(struct interface *i,const void *hw,const uint32_t *addr){
	send_arp_req(i,hw,addr,0ul);
}

#ifdef __cplusplus
}
#endif

#endif
