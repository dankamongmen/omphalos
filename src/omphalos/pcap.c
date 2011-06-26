#include <pcap.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <omphalos/ip.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/pcap.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static interface pcap_file_interface = {
	.fd = -1,
	.rfd = -1,
};

typedef struct pcap_marshal {
	interface *i;
	const omphalos_iface *octx;
} pcap_marshal;

// FIXME need to call back even on truncations etc. move function pointer
// to pcap_marshal and unify call to redirect + packet_read().
static void
handle_pcap_ethernet(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	pcap_marshal *pm = (pcap_marshal *)gi;
	interface *iface = pm->i; // interface for the pcap file
	omphalos_packet packet;

	++iface->frames;
	if(h->caplen != h->len){
		++iface->truncated;
		pm->octx->diagnostic("Partial capture (%u/%ub)",h->caplen,h->len);
		return;
	}
	packet.i = iface;
	handle_ethernet_packet(pm->octx,&packet,bytes,h->len);
	if(pm->octx->packet_read){
		pm->octx->packet_read(iface,iface->opaque,&packet);
	}
}

static void
handle_pcap_cooked(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	pcap_marshal *pm = (pcap_marshal *)gi;
	interface *iface = pm->i; // interface for the pcap file
	const struct pcapsll { // taken from pcap-linktype(7), "LINKTYPE_LINUX_SLL"
		uint16_t pkttype;
		uint16_t arptype;
		uint16_t hwlen;
		char hwaddr[8];
		uint16_t proto;
	} *sll;
	omphalos_packet packet;
	struct l2host *l2s;

	++iface->frames;
	if(h->caplen != h->len){
		pm->octx->diagnostic("Partial capture (%u/%ub)",h->caplen,h->len);
		++iface->truncated;
		return;
	}
	sll = (const struct pcapsll *)bytes;
	if(h->len < sizeof(*sll) || ntohs(sll->hwlen) > sizeof(sll->hwaddr)){
		++iface->malformed;
		return;
	}
	if((l2s = lookup_l2host(&iface->l2hosts,sll->hwaddr,ntohs(sll->hwlen))) == NULL){
		return;
	}
	// proto is in network byte-order. rather than possibly switch it
	// every time, we provide the cases in network byte-order
	switch(sll->proto){
		case __constant_ntohs(ETH_P_IP):{
			handle_ipv4_packet(pm->octx,iface,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}case __constant_ntohs(ETH_P_IPV6):{
			handle_ipv6_packet(pm->octx,iface,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}default:{
			++iface->noprotocol;
			break;
		}
	}
	if(pm->octx->packet_read){
		pm->octx->packet_read(iface,iface->opaque,&packet);
	}
}

int handle_pcap_file(const omphalos_ctx *pctx){
	pcap_handler fxn;
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_marshal pmarsh = {
		.octx = &pctx->iface,
	};
	pcap_t *pcap;

	pmarsh.i = &pcap_file_interface;
	free(pmarsh.i->name);
	memset(pmarsh.i,0,sizeof(*pmarsh.i));
	pmarsh.i->fd = pmarsh.i->rfd = -1;
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
			fxn = handle_pcap_ethernet;
			break;
		}case DLT_LINUX_SLL:{
			fxn = handle_pcap_cooked;
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

void cleanup_pcap(const omphalos_iface *octx){
	free_iface(octx,&pcap_file_interface);
}
