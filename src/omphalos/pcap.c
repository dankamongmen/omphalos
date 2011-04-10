#include <pcap.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/sll.h>
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
	handle_cooked_packet(iface,&h->ts,bytes,h->caplen);
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
	if((i->name = strdup(pctx->pcapfn)) == NULL){ // FIXME when to free?
		return -1;
	}
	if((pcap = pcap_open_offline(pctx->pcapfn,ebuf)) == NULL){
		fprintf(stderr,"Couldn't open %s (%s?)\n",pctx->pcapfn,ebuf);
		return -1;
	}
	fxn = NULL;
	switch(pcap_datalink(pcap)){
		case DLT_EN10MB:{
			fxn = handle_pcap_packet; // FIXME assumes cooked
			break;
		}case DLT_LINUX_SLL:{
			fxn = handle_pcap_packet;
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
