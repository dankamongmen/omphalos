#include <pcap.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <omphalos/ll.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <omphalos/psocket.h>

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -f filename ]\n",arg0);
	exit(ret);
}

static void
handle_packet(const struct timeval *tv,const void *frame,size_t len){
	if(len <= 99999){
		printf("[%5zub] %lu.%06lu %p\n",len,tv->tv_sec,tv->tv_usec,frame);
	}else{
		printf("[%zub] %lu.%06lu %p\n",len,tv->tv_sec,tv->tv_usec,frame);
	}
}

static void
handle_ring_packet(int fd,void *frame){
	struct tpacket_hdr *thdr = frame;
	struct timeval tv;

	while(thdr->tp_status == 0){
		struct pollfd pfd;
		int events;

		fprintf(stderr,"Packet not ready\n");
		pfd.fd = fd;
		pfd.revents = 0;
		pfd.events = POLLIN | POLLRDNORM | POLLERR;
		while((events = poll(&pfd,1,-1)) == 0){
			fprintf(stderr,"Interrupted polling packet socket %d\n",fd);
		}
		if(events < 0){
			fprintf(stderr,"Error polling packet socket %d (%s?)\n",fd,strerror(errno));
			return;
		}
		if(pfd.revents & POLLERR){
			fprintf(stderr,"Error polling packet socket %d\n",fd);
			return;
		}
	}
	if((thdr->tp_status & TP_STATUS_COPY) || thdr->tp_snaplen != thdr->tp_len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",thdr->tp_snaplen,thdr->tp_len);
		return;
	}
	tv.tv_sec = thdr->tp_sec;
	tv.tv_usec = thdr->tp_usec;
	handle_packet(&tv,(const char *)frame + thdr->tp_mac,thdr->tp_len);
	thdr->tp_status = TP_STATUS_KERNEL; // return the frame
}

static void
handle_pcap_packet(u_char *user,const struct pcap_pkthdr *h,const u_char *bytes){
	if(user){
		fprintf(stderr,"Unexpected parameter: %p\n",user);
		return;
	}
	if(h->caplen != h->len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",h->caplen,h->len);
		return;
	}
	handle_packet(&h->ts,bytes,h->caplen);
}

static int
handle_pcap_file(const char *pcapfn){
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;

	if((pcap = pcap_open_offline(pcapfn,ebuf)) == NULL){
		fprintf(stderr,"Couldn't open %s (%s?)\n",pcapfn,ebuf);
		return -1;
	}
	if(pcap_loop(pcap,-1,handle_pcap_packet,NULL)){
		fprintf(stderr,"Error processing pcap file %s (%s?)\n",pcapfn,pcap_geterr(pcap));
		pcap_close(pcap);
		return -1;
	}
	pcap_close(pcap);
	return 0;
}

int main(int argc,char * const *argv){
	const char *pcapfn = NULL;
	int opt;

	opterr = 0; // suppress getopt() diagnostic to stderr
	while((opt = getopt(argc,argv,":f:")) >= 0){
		switch(opt){
		case 'f':{
			if(pcapfn){
				fprintf(stderr,"Provided -f twice\n");
				usage(argv[0],EXIT_FAILURE);
			}
			pcapfn = optarg;
			break;
		}case ':':{
			fprintf(stderr,"Option requires argument: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
		}default:
			fprintf(stderr,"Unknown option: '%c'\n",optopt);
			usage(argv[0],EXIT_FAILURE);
		}
	}
	if(argv[optind]){ // don't allow trailing arguments
		fprintf(stderr,"Trailing argument: %s\n",argv[optind]);
		usage(argv[0],EXIT_FAILURE);
	}
	if(pcapfn){
		if(handle_pcap_file(pcapfn)){
			return EXIT_FAILURE;
		}
	}else{
		void *txm,*rxm;
		size_t ts,rs;
		int tfd,rfd;

		if((rfd = packet_socket(ETH_P_ALL)) < 0){
			return EXIT_FAILURE;
		}
		if((tfd = packet_socket(ETH_P_ALL)) < 0){
			return EXIT_FAILURE;
		}
		if((rs = mmap_rx_psocket(rfd,&rxm)) == 0){
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if((ts = mmap_tx_psocket(tfd,&txm)) == 0){
			unmap_psocket(rxm,rs);
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		handle_ring_packet(rfd,rxm); // FIXME
		if(unmap_psocket(txm,ts)){
			unmap_psocket(rxm,rs);
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if(unmap_psocket(rxm,rs)){
			close(rfd);
			close(tfd);
			return EXIT_FAILURE;
		}
		if(close(rfd)){
			fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",rfd,strerror(errno));
			close(tfd);
			return EXIT_FAILURE;
		}
		if(close(tfd)){
			fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",tfd,strerror(errno));
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
