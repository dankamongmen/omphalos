#include <linux/ip.h>
#include <sys/types.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/udp.h>
#include <omphalos/dns.h>
#include <omphalos/dhcp.h>
#include <omphalos/mdns.h>
#include <asm/byteorder.h>
#include <omphalos/service.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME we want an automata-based approach to generically match. until then,
// use well-known ports, ugh...
void handle_udp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct udphdr *udp = frame;
	const void *ubdy;
	uint16_t ulen;

	if(len < sizeof(*udp)){
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	ubdy = (const char *)udp + sizeof(*udp);
	ulen = len - sizeof(*udp);
	op->l4src = udp->source;
	op->l4dst = udp->dest;
	switch(udp->source){
		case __constant_htons(DNS_UDP_PORT):{
			if(handle_dns_packet(op,ubdy,ulen) == 0){
				observe_service(op->i,op->l2s,op->l3s,op->l3proto,
					op->l4src,"DNS",NULL);
			}
		}break;
		case __constant_htons(MDNS_UDP_PORT):{
			// FIXME also check dest?
			handle_mdns_packet(op,ubdy,ulen);
		}break;
		case __constant_htons(DHCP_UDP_PORT):{
			// FIXME also check dest?
			handle_dhcp_packet(octx,op,ubdy,ulen);
		}break;
	}
}

// hdr must be a valid ipv4 header
uint16_t udp4_csum(const void *hdr){
	const struct iphdr *ih = hdr;
	const struct udphdr *uh = (const void *)((const char *)hdr + (ih->ihl << 2u));
	uint16_t dlen = ntohs(uh->len);
	const uint16_t *cur;
	uint32_t sum,fold;
	unsigned z;

	sum = 0;
	// UDP4 checksum is over UDP header, UDP data (zero padded to make it a
	// multiple of 16 bits), and 12-byte IPv4 pseudoheader containing
	// source addr, dest addr, 8 bits of 0, protocol, and total UDP length.
	cur = (const uint16_t *)&ih->saddr;
	sum += cur[0];
	sum += cur[1];
	cur = (const uint16_t *)&ih->daddr;
	sum += cur[0];
	sum += cur[1];
	sum += htons(ih->protocol); // zeroes and protocol

	sum += htons(dlen); // total length
	cur = (const uint16_t *)uh; // now checksum over UDP header + data
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)uh)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}

// hdr must be a valid ipv6 header
uint16_t udp6_csum(const void *hdr){
	const struct ip6_hdr *ih = hdr;
	// FIXME doesn't work for more than one IPv6 header!
	const struct udphdr *uh = (const struct udphdr *)((const unsigned char *)hdr + 40);
	uint16_t dlen = ntohs(uh->len);
	const uint16_t *cur;
	uint16_t fold;
	uint32_t sum;
	unsigned z;

	sum = 0;
	// UDP6 checksum is over UDP header, UDP data (zero padded to make it a
	// multiple of 16 bits), and 40-byte IPv6 pseudoheader containing
	// source addr, dest addr, UDP length, 24 bits of 0 and next header
	sum += ih->ip6_src.s6_addr16[0];
	sum += ih->ip6_src.s6_addr16[1];
	sum += ih->ip6_src.s6_addr16[2];
	sum += ih->ip6_src.s6_addr16[3];
	sum += ih->ip6_src.s6_addr16[4];
	sum += ih->ip6_src.s6_addr16[5];
	sum += ih->ip6_src.s6_addr16[6];
	sum += ih->ip6_src.s6_addr16[7]; // saddr
	sum += ih->ip6_dst.s6_addr16[0];
	sum += ih->ip6_dst.s6_addr16[1];
	sum += ih->ip6_dst.s6_addr16[2];
	sum += ih->ip6_dst.s6_addr16[3];
	sum += ih->ip6_dst.s6_addr16[4];
	sum += ih->ip6_dst.s6_addr16[5];
	sum += ih->ip6_dst.s6_addr16[6];
	sum += ih->ip6_dst.s6_addr16[7]; // daddr
	sum += ih->ip6_ctlun.ip6_un1.ip6_un1_plen;
	sum += htons(17); // zeroes and protocol
	cur = (const uint16_t *)uh; // now checksum over UDP header + data
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)uh)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}
