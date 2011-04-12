#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <asm/byteorder.h>
#include <omphalos/eapol.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

void handle_ethernet_packet(interface *i,const void *frame,size_t len){
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
					handle_ipv4_packet(i,dgram,dlen);
					break;
				}case __constant_ntohs(ETH_P_ARP):{
					handle_arp_packet(i,dgram,dlen);
					break;
				}case __constant_ntohs(ETH_P_IPV6):{
					handle_ipv6_packet(i,dgram,dlen);
					break;
				}case __constant_ntohs(ETH_P_PAE):{
					handle_pae_packet(i,dgram,dlen);
					break;
				}default:{
					printf("%s noproto for 0x%x\n",__func__,hdr->h_proto);
					++i->noprotocol;
					break;
				}
			}
		}
	}
}
