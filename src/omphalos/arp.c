#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <linux/if_arp.h>
#include <omphalos/arp.h>
#include <omphalos/diag.h>
#include <asm/byteorder.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

static const unsigned char PROBESRC[4] = {};

void handle_arp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct arphdr *ap = frame;
	const void *saddr,*daddr;
	int fam;

	if(len < sizeof(*ap)){
		op->malformed = 1;
		return;
	}
	if(len < sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2){
		op->malformed = 1;
		diagnostic("%s %s bad length expected %zu got %zu",
			__func__,op->i->name,sizeof(*ap) + ap->ar_hln * 2 + ap->ar_pln * 2,len);
		return;
	}
	if(op->i->addrlen != ap->ar_hln){
		op->malformed = 1;
		diagnostic("%s %s malformed expected %zu got %d",
			__func__,op->i->name,op->i->addrlen,ap->ar_hln);
		return;
	}
	switch(ap->ar_pro){
		case __constant_htons(ETH_P_IP):
			if(ap->ar_pln == sizeof(uint32_t)){
				fam = AF_INET;
			}else{
				op->malformed = 1;
				diagnostic("%s %s nw malformed expected %zu got %u",
					__func__,op->i->name,sizeof(uint32_t),ap->ar_pln);
				return;
			}
			break;
		default:
			op->noproto = 1;
			diagnostic("%s %s noproto for %u",__func__,
					op->i->name,ap->ar_pro);
			return;
			break;
	}
	saddr = (const char *)ap + sizeof(*ap) + ap->ar_hln;
	// ARP probes as specified by RFC 5227 set the source address to
	// 0.0.0.0; these oughtn't be linked to the hardware addresses. We
	// probably only ought admit LOCAL/UNICASTs FIXME.
	if(ap->ar_pln <= sizeof(PROBESRC)){
		if(memcmp(PROBESRC,saddr,ap->ar_pln)){
			op->l3s = lookup_local_l3host(&op->tv,op->i,op->l2s,fam,saddr);
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
					op->l3d = lookup_local_l3host(&op->tv,op->i,op->l2d,fam,daddr);
				}
			}
		}
		break;
	}default:{
		op->noproto = 1;
		diagnostic("%s %s unknown ARP op %u",__func__,op->i->name,ap->ar_op);
		break;
	}}
}

static void
prepare_arp_probe(const interface *i,void *frame,size_t *flen,
			const void *haddr,size_t hln,const uint32_t *paddr,
			const uint32_t *saddr){
	struct tpacket_hdr *thdr;
	unsigned char *payload;
	struct ethhdr *ehdr;
	struct arphdr *ahdr;
	size_t tlen,pln;

	pln = sizeof(*paddr);
	thdr = frame;
	if(*flen < sizeof(*thdr)){
		diagnostic("%s %s frame too small for tx",__func__,i->name);
		return;
	}
	tlen = thdr->tp_mac + sizeof(*ehdr) + sizeof(*ahdr)
			+ 2 * hln + 2 * pln;
	if(*flen < tlen){
		diagnostic("%s %s frame too small for tx",__func__,i->name);
		return;
	}
	assert(hln == i->addrlen); // FIXME handle this case
	// FIXME what about non-ethernet
	ehdr = (struct ethhdr *)((char *)frame + thdr->tp_mac);
	assert(prep_eth_header(ehdr,*flen - thdr->tp_mac,i,haddr,ETH_P_ARP) == sizeof(struct ethhdr));
	thdr->tp_len = sizeof(struct ethhdr) + sizeof(struct arphdr)
		+ hln * 2 + pln * 2;
	ahdr = (struct arphdr *)((char *)ehdr + sizeof(*ehdr));
	ahdr->ar_hrd = htons(ARPHRD_ETHER);
	ahdr->ar_pro = htons(ETH_P_IP);
	ahdr->ar_hln = hln;
	ahdr->ar_pln = pln;
	ahdr->ar_op = htons(ARPOP_REQUEST);
	// FIXME this is all horribly unsafe
	payload = (unsigned char *)ahdr + sizeof(*ahdr);
	// FIXME allow for spoofing
	memcpy(payload,i->addr,hln);
	memcpy(payload + hln,saddr,pln);
	// FIXME need a source network address
	memcpy(payload + hln + pln,haddr,hln);
	memcpy(payload + hln + pln + hln,paddr,pln);
	*flen = tlen;
}

void send_arp_req(interface *i,const void *hwaddr,const uint32_t *addr,
						const uint32_t *saddr){
	void *frame;
	size_t flen;

	if(i->flags & IFF_NOARP){
		return;
	}
	if( (frame = get_tx_frame(i,&flen)) ){
		/*char addrstr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET,addr,addrstr,sizeof(addrstr));
		diagnostic("Probing %s on %s",addrstr,i->name);*/
		prepare_arp_probe(i,frame,&flen,hwaddr,i->addrlen,addr,saddr);
		send_tx_frame(i,frame);
	}
}
