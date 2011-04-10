#include <pcap.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/pcap.h>
#include <linux/if_ether.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static interface pcap_file_interface;

static void
handle_pcap_packet(u_char *gi,const struct pcap_pkthdr *h,const u_char *bytes){
	interface *iface = (interface *)gi; // interface for the pcap file

	if(h->caplen != h->len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",h->caplen,h->len);
		return;
	}
	// FIXME verify link type! see pcap-linktype(7)
	handle_ethernet_packet(iface,&h->ts,bytes,h->caplen,
			((const struct ethhdr *)bytes)->h_dest);
}

int handle_pcap_file(const omphalos_ctx *pctx){
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;
	interface *i;

	i = &pcap_file_interface;
	free(i->name);
	memset(i,0,sizeof(*i));
	// FIXME set up remainder of interface as best we can...
	if((i->name = strdup(pctx->pcapfn)) == NULL){ // FIXME when to free?
		return -1;
	}
	if((pcap = pcap_open_offline(pctx->pcapfn,ebuf)) == NULL){
		fprintf(stderr,"Couldn't open %s (%s?)\n",pctx->pcapfn,ebuf);
		return -1;
	}
	if(pcap_loop(pcap,-1,handle_pcap_packet,(u_char *)i)){
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
