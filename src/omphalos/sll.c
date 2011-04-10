#include <arpa/inet.h>
#include <omphalos/sll.h>
#include <linux/if_arp.h>
#include <asm/byteorder.h>
#include <linux/if_packet.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

// A cooked packet has had its link-layer header stripped. An l2-neutral
// struct sockaddr_sll is put in its place.
void handle_cooked_packet(interface *iface,const unsigned char *pkt,size_t len){
	struct sockaddr_ll *sll = (struct sockaddr_ll *)pkt;

	if(len < sizeof(*sll)){
		++iface->malformed;
		return;
	}
	// sll_protocol is in network byte-order. rather than possibly
	// switch it every time, we provide the cases in network byte-order
	switch(sll->sll_protocol){
		case __constant_ntohs(ARPHRD_ETHER):{
			handle_ethernet_packet(iface,pkt + sizeof(*sll),len - sizeof(*sll));
			break;
		}default:{
			++iface->noprotocol;
			break;
		}
	}
}
