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

static interface pcap_file_interface;

static void
handle_pcap_ethernet(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	interface *iface = (interface *)gi; // interface for the pcap file

	++iface->frames;
	if(h->caplen != h->len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",h->caplen,h->len);
		++iface->truncated;
		return;
	}
	handle_ethernet_packet(iface,bytes,h->len);
}

static void
handle_pcap_cooked(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	interface *iface = (interface *)gi; // interface for the pcap file
	const struct pcapsll { // taken from pcap-linktype(7), "LINKTYPE_LINUX_SLL"
		uint16_t pkttype;
		uint16_t arptype;
		uint16_t hwlen;
		char hwaddr[8];
		uint16_t proto;
	} *sll;
	struct l2host *l2s;

	++iface->frames;
	if(h->caplen != h->len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",h->caplen,h->len);
		++iface->truncated;
		return;
	}
	sll = (const struct pcapsll *)bytes;
	if(h->len < sizeof(*sll) || be16toh(sll->hwlen) > sizeof(sll->hwaddr)){
		++iface->malformed;
		return;
	}
	if((l2s = lookup_l2host(sll->hwaddr,be16toh(sll->hwlen))) == NULL){
		return;
	}
	// proto is in network byte-order. rather than possibly switch it
	// every time, we provide the cases in network byte-order
	switch(sll->proto){
		case __constant_ntohs(ETH_P_IP):{
			handle_ipv4_packet(iface,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}case __constant_ntohs(ETH_P_IPV6):{
			handle_ipv6_packet(iface,bytes + sizeof(*sll),h->len - sizeof(*sll));
			break;
		}default:{
			++iface->noprotocol;
			break;
		}
	}
}

int handle_pcap_file(const omphalos_ctx *pctx){
	pcap_handler fxn;
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;
	interface *i;

	i = &pcap_file_interface;
	free(i->name);
	memset(i,0,sizeof(*i));
	// FIXME set up remainder of interface as best we can...
	if((i->name = strdup(pctx->pcapfn)) == NULL){
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
	if(pcap_loop(pcap,-1,fxn,(u_char *)i)){
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

void cleanup_pcap(void){
	free_iface(&pcap_file_interface);
}
