#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/psocket.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Acquire a frame from the ringbuffer
void *get_tx_frame(const omphalos_iface *octx,interface *i,size_t *fsize){
	struct tpacket_hdr *thdr = i->curtxm;
	void *ret;

	if(thdr == NULL){
		octx->diagnostic("Can't transmit on %s (fd %d)",i->name,i->fd);
		return NULL;
	}
	// FIXME need also check for TP_WRONG_FORMAT methinks?
	if(thdr->tp_status != TP_STATUS_AVAILABLE){
		octx->diagnostic("No available TX frames on %s",i->name);
		return NULL;
	}
	ret = i->curtxm;
	i->curtxm += inclen(&i->txidx,&i->ttpr);
	*fsize = i->ttpr.tp_frame_size;
	return ret;
}

// Mark a frame as ready-to-send. Must have come from get_tx_frame() using this
// same interface. Yes, we will see packets we generate on the RX ring.
void send_tx_frame(const omphalos_iface *octx,interface *i,void *frame){
	struct tpacket_hdr *thdr = frame;
	uint32_t tplen = thdr->tp_len;

	thdr->tp_status = TP_STATUS_SEND_REQUEST;
	if(send(i->fd,NULL,0,0) < 0){
		octx->diagnostic("Error transmitting on %s",i->name);
		++i->txerrors;
	}else{
		i->txbytes += tplen;
		++i->txframes;
	}
}

void abort_tx_frame(interface *i,void *frame){
	struct tpacket_hdr *thdr = frame;

	++i->txaborts;
	thdr->tp_status = TP_STATUS_AVAILABLE;
}

// FIXME
/*void prepare_arp_req(const omphalos_iface *octx,const interface *i,
		void *frame,size_t *flen,const void *paddr,size_t pln){
	assert(octx && i && frame && flen && paddr && pln);
}*/

void prepare_arp_probe(const omphalos_iface *octx,const interface *i,
		void *frame,size_t *flen,const void *haddr,size_t hln,
		const void *paddr,size_t pln,const void *saddr){
	struct tpacket_hdr *thdr;
	unsigned char *payload;
	struct ethhdr *ehdr;
	struct arphdr *ahdr;
	size_t tlen;

	tlen = sizeof(*thdr) + sizeof(*ehdr) + sizeof(*ahdr)
			+ 2 * hln + 2 * pln;
	if(*flen < tlen){
		octx->diagnostic("%s %s frame too small for tx",__func__,i->name);
		return;
	}
	assert(hln == i->addrlen); // FIXME handle this case
	thdr = frame;
	// FIXME what about non-ethernet
	ehdr = (struct ethhdr *)((char *)frame + sizeof(*thdr));
	assert(prep_eth_header(ehdr,*flen - sizeof(*thdr),i,haddr,ETH_P_ARP) == sizeof(struct ethhdr));
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
