#include <sys/types.h>
#include <linux/udp.h>
#include <omphalos/udp.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <asm/byteorder.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define DNS_UDP_PORT 53
#define MDNS_UDP_PORT 5353

// FIXME we want an automata-based approach to generically match. until then,
// use well-known ports, ugh...
void handle_udp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct udphdr *udp = frame;
	const void *ubdy;
	uint16_t ulen;

	if(len < sizeof(*udp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	ubdy = (const char *)udp + sizeof(*udp);
	ulen = len - sizeof(*udp);
	switch(udp->source){
		case __constant_htons(DNS_UDP_PORT):{
			handle_dns_packet(octx,op,ubdy,ulen);
		}break;
		case __constant_htons(MDNS_UDP_PORT):{
			handle_mdns_packet(octx,op,ubdy,ulen);
		}break;
	}
}
