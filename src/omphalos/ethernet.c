#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <asm/byteorder.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#include <arpa/inet.h>

void handle_ethernet_packet(struct interface *i,const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	struct l2host *l2s,*l2d;

	if(len < sizeof(*hdr)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	if( (l2s = lookup_l2host(hdr->h_source,ETH_ALEN)) ){
		if( (l2d = lookup_l2host(hdr->h_dest,ETH_ALEN)) ){
			const void *dgram = (const char *)frame + sizeof(*hdr);
			size_t dlen = len - sizeof(*hdr);

			switch(hdr->h_proto){
				case __constant_ntohs(ETH_P_IP):{
					handle_ip_packet(i,dgram,dlen);
					break;
				}case __constant_ntohs(ETH_P_ARP):{
					handle_arp_packet(i,dgram,dlen);
					break;
				}case __constant_ntohs(ETH_P_IPV6):{
					handle_ipv6bb_packet(i,dgram,dlen);
					break;
				}default:{
					printf("%s noproto for %u\n",__func__,hdr->h_proto);
					++i->noprotocol;
					break;
				}
			}
		}
	}
}
