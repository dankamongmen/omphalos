#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <linux/if_arp.h>
#include <omphalos/arp.h>
#include <asm/byteorder.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

static const unsigned char PROBESRC[16] = {};

void handle_arp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct arphdr *ap = frame;
	const void *saddr,*daddr;
	int fam;

	if(len < sizeof(*ap)){
		op->malformed = 1;
		return;
	}
	if(check_ethernet_padup(len,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2)){
		op->malformed = 1;
		octx->diagnostic(L"%s %s bad length expected %zu got %zu",
			__func__,op->i->name,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2,len);
		return;
	}
	if(op->i->addrlen != ap->ar_hln){
		op->malformed = 1;
		octx->diagnostic(L"%s %s malformed expected %u got %u",
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
				op->malformed = 1;
				octx->diagnostic(L"%s %s nw malformed expected %u got %u",
					__func__,op->i->name,sizeof(uint32_t),ap->ar_pln);
				return;
			}
			break;
		default:
			op->noproto = 1;
			octx->diagnostic(L"%s %s noproto for %u",__func__,
					op->i->name,ap->ar_pro);
			return;
			break;
	}
	saddr = (const char *)ap + sizeof(*ap) + ap->ar_hln;
	// ARP probes as specified by RFC 5227 set the source address to
	// 0.0.0.0; these oughtn't be linked to the hardware addresses.
	if(ap->ar_pln <= sizeof(PROBESRC)){
		if(memcmp(PROBESRC,saddr,ap->ar_pln)){
			op->l3s = lookup_local_l3host(op->i,op->l2s,fam,saddr);
		}
	}
	switch(ap->ar_op){
	case __constant_ntohs(ARPOP_REQUEST):{ // FIXME reply with ARP spoof...
		break;
	}case __constant_ntohs(ARPOP_REPLY):{
		int cat;

		// Sometimes, arp replies will be sent to the medium's broadcast
		// address, but a host's network address. Don't look up l3d for
		// a broadcast address. Hack! FIXME what we really want to do
		// is issue an ARP request for any address seen here...
		cat = l2categorize(op->i,op->l2d);
		if(cat == RTN_LOCAL || cat == RTN_UNICAST){
			daddr = (const char *)ap + sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln;
			if(ap->ar_pln <= sizeof(PROBESRC)){
				if(memcmp(PROBESRC,daddr,ap->ar_pln)){
					op->l3d = lookup_local_l3host(op->i,op->l2d,fam,daddr);
				}
			}
		}
		break;
	}default:{
		op->noproto = 1;
		octx->diagnostic(L"%s %s unknown ARP op %u",__func__,
					op->i->name,ap->ar_op);
		break;
	}}
}

void send_arp_probe(interface *i,const void *hwaddr,const uint32_t *addr,
						const uint32_t *saddr){
	void *frame;
	size_t flen;

	if( (frame = get_tx_frame(i,&flen)) ){
		/*char addrstr[INET6_ADDRSTRLEN];
		inet_ntop(addrlen == 4 ? AF_INET:AF_INET6,addr,addrstr,sizeof(addrstr));
		diagnostic(L"Probing %s on %s",addrstr,i->name);*/
		prepare_arp_probe(i,frame,&flen,hwaddr,i->addrlen,addr,saddr);
		send_tx_frame(i,frame);
	}
}
