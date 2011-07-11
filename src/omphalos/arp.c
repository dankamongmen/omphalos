#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/arp.h>
#include <asm/byteorder.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

void handle_arp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct arphdr *ap = frame;
	const void *saddr,*daddr;

	if(len < sizeof(*ap)){
		++op->i->malformed;
		return;
	}
	if(check_ethernet_padup(len,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2)){
		++op->i->malformed;
		octx->diagnostic("%s malformed expected %zu got %zu",
			__func__,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2,len);
		return;
	}
	saddr = (const char *)ap + sizeof(*ap) + ap->ar_hln;
	switch(ap->ar_op){
	case __constant_ntohs(ARPOP_REQUEST):{
		name_l2host(octx,op->i,op->l2s,AF_INET,saddr);
		// FIXME reply with ARP spoof...
	break;}case __constant_ntohs(ARPOP_REPLY):{
		name_l2host_local(octx,op->i,op->l2s,AF_INET,saddr);
		daddr = (const char *)ap + sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln;
		name_l2host(octx,op->i,op->l2d,AF_INET,daddr);
	break;}default:{
		++op->i->noprotocol;
		octx->diagnostic("%s %s unknown ARP op %u",__func__,
					op->i->name,ap->ar_op);
	break;}}
}
