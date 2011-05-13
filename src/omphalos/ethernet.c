#include <sys/socket.h>
#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <omphalos/util.h>
#include <omphalos/eapol.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static void
handle_8021q(const omphalos_iface *octx,interface *i,const void *frame,
						size_t len){
	const struct ethhdr *hdr = frame;
	const unsigned char *type;
	uint16_t proto;
	const void *dgram;
	size_t dlen;
	
	// l2s and l2d were already looked up; pass them in if we need 'em
	if(len < sizeof(*hdr) + 4){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	type = ((const unsigned char *)frame + ETH_ALEN * 2 + 4);
	proto = ntohs(*(const uint16_t *)type);
 	dgram = (const char *)frame + sizeof(*hdr) + 4;
	dlen = len - sizeof(*hdr) - 4;
	switch(proto){
	case ETH_P_IP:{
		handle_ipv4_packet(i,dgram,dlen);
	break;}case ETH_P_ARP:{
		handle_arp_packet(i,dgram,dlen);
	break;}case ETH_P_IPV6:{
		handle_ipv6_packet(i,dgram,dlen);
	break;}case ETH_P_PAE:{
		handle_eapol_packet(i,dgram,dlen);
	break;}default:{
		if(proto <= ETH_DATA_LEN){
			// FIXME handle IEEE 802.3
		}else{
			++i->noprotocol;
			octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,proto);
		}
	break;} }
}

void handle_ethernet_packet(const omphalos_iface *octx,interface *i,
					const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	struct l2host *l2s,*l2d;

	if(len < sizeof(*hdr)){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	if( (l2s = lookup_l2host(hdr->h_source,ETH_ALEN)) ){
		if( (l2d = lookup_l2host(hdr->h_dest,ETH_ALEN)) ){
			const void *dgram = (const char *)frame + sizeof(*hdr);
			uint16_t proto = ntohs(hdr->h_proto);
			size_t dlen = len - sizeof(*hdr);

			switch(proto){
				case ETH_P_IP:{
					handle_ipv4_packet(i,dgram,dlen);
					break;
				}case ETH_P_ARP:{
					handle_arp_packet(i,dgram,dlen);
					break;
				}case ETH_P_IPV6:{
					handle_ipv6_packet(i,dgram,dlen);
					break;
				}case ETH_P_PAE:{
					handle_eapol_packet(i,dgram,dlen);
					break;
				}case ETH_P_8021Q:{
					handle_8021q(octx,i,frame,len);
					break;
				}default:{
					if(proto <= ETH_DATA_LEN){
						// FIXME handle IEEE 802.3
					}else{
						octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,proto);
						++i->noprotocol;
					}
					break;
				}
			}
		}
	}
}
