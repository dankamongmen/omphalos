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
	int fam;

	if(len < sizeof(*ap)){
		++op->i->malformed;
		return;
	}
	if(check_ethernet_padup(len,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2)){
		++op->i->malformed;
		octx->diagnostic("%s %s bad length expected %zu got %zu",
			__func__,op->i->name,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2,len);
		return;
	}
	if(op->i->addrlen != ap->ar_hln){
		++op->i->malformed;
		octx->diagnostic("%s %s malformed expected %u got %u",
			__func__,op->i->name,op->i->addrlen,ap->ar_hln);
		return;
	}
	switch(ap->ar_pro){
		case __constant_htons(ETH_P_IP):
			if(ap->ar_pln == sizeof(uint32_t)){
				fam = AF_INET;
			}else if(ap->ar_pln == sizeof(uint32_t) * 4){
				fam = AF_INET6;
			}else{
				++op->i->malformed;
				octx->diagnostic("%s %s nw malformed expected %u got %u",
					__func__,op->i->name,sizeof(uint32_t),ap->ar_pln);
				return;
			}
			break;
		default:
			++op->i->noprotocol;
			octx->diagnostic("%s %s noproto for %u",__func__,
					op->i->name,ap->ar_pro);
			return;
	}
	saddr = (const char *)ap + sizeof(*ap) + ap->ar_hln;
	switch(ap->ar_op){
	case __constant_ntohs(ARPOP_REQUEST):{
		name_l2host_local(octx,op->i,op->l2s,fam,saddr);
		// FIXME reply with ARP spoof...
	break;}case __constant_ntohs(ARPOP_REPLY):{
		name_l2host_local(octx,op->i,op->l2s,fam,saddr);
		daddr = (const char *)ap + sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln;
		name_l2host(octx,op->i,op->l2d,fam,daddr);
	break;}default:{
		++op->i->noprotocol;
		octx->diagnostic("%s %s unknown ARP op %u",__func__,
					op->i->name,ap->ar_op);
	break;}}
}
