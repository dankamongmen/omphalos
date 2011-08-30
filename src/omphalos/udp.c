#include <linux/ip.h>
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

// hdr must be a valid ipv4 header
uint16_t udp4_csum(const void *hdr){
	const struct iphdr *ih = hdr;
	const struct udphdr *uh = (const void *)((const char *)hdr + (ih->ihl << 2u));
	const void *data = (const char *)uh + sizeof(*uh);
	uint16_t dlen = ntohs(uh->len);
	const uint16_t *cur;
	uint16_t sum,fold;
	unsigned z;

	sum = 0;
	sum += (ih->saddr & 0xffffu) + (ih->saddr >> 16u);
	sum += (ih->daddr & 0xffffu) + (ih->daddr >> 16u);
	sum += ih->protocol;
	sum += dlen + sizeof(*uh);
	cur = (const uint16_t *)uh;
	for(z = 0 ; z < (sizeof(*uh) + dlen) / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)data)[dlen - 1])) << 8u;
	}
	fold = 0;
	for(z = 0 ; z < sizeof(*cur) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	if((fold = ~fold) == 0u){
		fold = 0xffffu;
	}
	return fold;
}

