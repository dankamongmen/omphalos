#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pcap/pcap.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <omphalos/ip.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/pcap.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/omphalos.h>
#include <omphalos/netaddrs.h>
#include <omphalos/interface.h>

static pcap_dumper_t *dumper;
static pthread_mutex_t dumplock = PTHREAD_MUTEX_INITIALIZER;

static interface pcap_file_interface = {
	.fd = -1,
	.rfd = -1,
};

typedef struct pcap_marshal {
	interface *i;
	const omphalos_iface *octx;
	void (*handler)(const omphalos_iface *,omphalos_packet *,const void *,size_t);
} pcap_marshal;

// FIXME need to call back even on truncations etc. move function pointer
// to pcap_marshal and unify call to redirect + packet_read().
static void
handle_pcap_direct(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	pcap_marshal *pm = (pcap_marshal *)gi;
	interface *iface = pm->i; // interface for the pcap file
	omphalos_packet packet;

	++iface->frames;
	if(h->caplen != h->len){
		++iface->truncated;
		pm->octx->diagnostic("Partial capture (%u/%ub)",h->caplen,h->len);
		return;
	}
	memset(&packet,0,sizeof(packet));
	packet.i = iface;
	pm->handler(pm->octx,&packet,bytes,h->len);
	if(packet.l2s){
		l2srcpkt(packet.l2s);
	}
	if(packet.l2d){
		l2dstpkt(packet.l2d);
	}
	if(packet.l3s){
		l3_srcpkt(packet.l3s);
	}
	if(packet.l3d){
		l3_dstpkt(packet.l3d);
	}
	if(pm->octx->packet_read){
		pm->octx->packet_read(&packet);
	}
}

static void
handle_pcap_cooked(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	char bcast[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	pcap_marshal *pm = (pcap_marshal *)gi;
	interface *iface = pm->i; // interface for the pcap file
	const struct pcapsll { // taken from pcap-linktype(7), "LINKTYPE_LINUX_SLL"
		uint16_t pkttype;
		uint16_t arptype;
		uint16_t hwlen;
		char hwaddr[8];
		uint16_t proto;
	} *sll;
	char addr[8];
	omphalos_packet packet;

	++iface->frames;
	if(h->caplen != h->len || h->caplen < sizeof(*sll)){
		pm->octx->diagnostic("Partial capture (%u/%ub)",h->caplen,h->len);
		++iface->truncated;
		return;
	}
	sll = (const struct pcapsll *)bytes;
	if(h->len < sizeof(*sll) || ntohs(sll->hwlen) > sizeof(sll->hwaddr)){
		++iface->malformed;
		return;
	}
	packet.i = iface;
	packet.i->addrlen = ntohs(sll->hwlen);
	assert(packet.i->addrlen <= sizeof(addr));
	memcpy(addr,sll->hwaddr,packet.i->addrlen);
	packet.i->addr = addr;
	packet.i->bcast = bcast;
	packet.l2s = lookup_l2host(pm->octx,iface,sll->hwaddr);
	packet.l2d = packet.l2s;
	packet.l3proto = ntohs(sll->proto);
	// proto is in network byte-order. rather than possibly switch it
	// every time, we provide the cases in network byte-order
	switch(sll->proto){
		case __constant_ntohs(ETH_P_IP):{
			handle_ipv4_packet(pm->octx,&packet,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}case __constant_ntohs(ETH_P_IPV6):{
			handle_ipv6_packet(pm->octx,&packet,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}default:{
			++iface->noprotocol;
			break;
		}
	}
	if(packet.l2s){
		l2srcpkt(packet.l2s);
	}
	if(packet.l2d){
		l2dstpkt(packet.l2d);
	}
	if(packet.l3s){
		l3_srcpkt(packet.l3s);
	}
	if(packet.l3d){
		l3_dstpkt(packet.l3d);
	}
	if(pm->octx->packet_read){
		pm->octx->packet_read(&packet);
	}
	packet.i->addr = NULL;
	packet.i->bcast = NULL;
}

int handle_pcap_file(const omphalos_ctx *pctx){
	pcap_handler fxn;
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_marshal pmarsh = {
		.octx = &pctx->iface,
		.i = &pcap_file_interface,
	};
	pcap_t *pcap;

	free(pmarsh.i->name);
	memset(pmarsh.i,0,sizeof(*pmarsh.i));
	pmarsh.i->fd = pmarsh.i->rfd = -1;
	pmarsh.i->flags = IFF_BROADCAST | IFF_UP | IFF_LOWER_UP;
	// FIXME set up remainder of interface as best we can...
	if((pmarsh.i->name = strdup(pctx->pcapfn)) == NULL){
		return -1;
	}
	if((pcap = pcap_open_offline(pctx->pcapfn,ebuf)) == NULL){
		fprintf(stderr,"Couldn't open %s (%s?)\n",pctx->pcapfn,ebuf);
		return -1;
	}
	fxn = NULL;
	switch(pcap_datalink(pcap)){
		case DLT_EN10MB:{
			fxn = handle_pcap_direct;
			pmarsh.handler = handle_ethernet_packet;
			pmarsh.i->addrlen = ETH_ALEN;
			pmarsh.i->addr = malloc(pmarsh.i->addrlen);
			pmarsh.i->bcast = malloc(pmarsh.i->addrlen);
			memset(pmarsh.i->addr,0,pmarsh.i->addrlen);
			memset(pmarsh.i->bcast,0xff,pmarsh.i->addrlen);
			break;
		}case DLT_LINUX_SLL:{
			fxn = handle_pcap_cooked;
			break;
		}case DLT_IEEE802_11_RADIO:{
			pmarsh.handler = handle_radiotap_packet;
			fxn = handle_pcap_direct;
			break;
		}default:{
			fprintf(stderr,"Unhandled datalink type: %d\n",pcap_datalink(pcap));
			break;
		}
	}
	if(fxn == NULL){
		pcap_close(pcap);
		return -1;
	}
	if(pcap_loop(pcap,-1,fxn,(u_char *)&pmarsh)){
		fprintf(stderr,"Error processing pcap file %s (%s?)\n",pctx->pcapfn,pcap_geterr(pcap));
		pcap_close(pcap);
		return -1;
	}
	pcap_close(pcap);
	return 0;
}

int print_pcap_stats(FILE *fp,interface *agg){
	const interface *iface;

	iface = &pcap_file_interface;
	if(iface->name){
		if(print_iface_stats(fp,iface,agg,"file") < 0){
			return -1;
		}
	}
	return 0;
}

int init_pcap(const omphalos_ctx *pctx){
	if(pctx->plog){
		dumper = pctx->plog;
	}
	return 0;
}

void cleanup_pcap(const omphalos_ctx *pctx){
	free_iface(&pctx->iface,&pcap_file_interface);
	pthread_mutex_lock(&dumplock);
	if(dumper){
		pcap_dump_flush(dumper);
		dumper = NULL;
	}
	if(pctx->plogp){
		pcap_close(pctx->plogp);
	}
	pthread_mutex_unlock(&dumplock);
}

int log_pcap_packet(struct pcap_pkthdr *h,void *sp){
	if(!dumper){
		return 0;
	}
	if(pthread_mutex_lock(&dumplock)){
		return -1;
	}
	pcap_dump((u_char *)dumper,h,sp);
	if(pthread_mutex_unlock(&dumplock)){
		return -1;
	}
	return 0;
}
