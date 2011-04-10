#include <omphalos/ip.h>
#include <asm/byteorder.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#include <arpa/inet.h>

void handle_ethernet_packet(const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	struct l2host *l2s,*l2d;

	if(len < sizeof(*hdr)){
		// FIXME malformed
		return;
	}
	if( (l2s = lookup_l2host(hdr->h_source,ETH_ALEN)) ){
		if( (l2d = lookup_l2host(hdr->h_dest,ETH_ALEN)) ){
			switch(hdr->h_proto){
				case __constant_ntohs(ETH_P_IP):{
					handle_ip_packet((const char *)frame + sizeof(*hdr),len - sizeof(*hdr));
					break;
				}default:{
					// FIXME malformed stat
					break;
				}
			}
		}
	}
}
