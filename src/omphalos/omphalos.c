#include <pcap.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <omphalos/ll.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <omphalos/netlink.h>
#include <omphalos/psocket.h>

typedef struct omphalos_ctx {
	const char *pcapfn;
	unsigned long count;
} omphalos_ctx;

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -f filename ] [ -c count ]\n",arg0);
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

		// fprintf(stderr,"Packet not ready\n");
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
handle_pcap_file(const omphalos_ctx *pctx){
	char ebuf[PCAP_ERRBUF_SIZE];
	pcap_t *pcap;

	if((pcap = pcap_open_offline(pctx->pcapfn,ebuf)) == NULL){
		fprintf(stderr,"Couldn't open %s (%s?)\n",pctx->pcapfn,ebuf);
		return -1;
	}
	if(pcap_loop(pcap,-1,handle_pcap_packet,NULL)){
		fprintf(stderr,"Error processing pcap file %s (%s?)\n",pctx->pcapfn,pcap_geterr(pcap));
		pcap_close(pcap);
		return -1;
	}
	pcap_close(pcap);
	return 0;
}

static inline
ssize_t inclen(unsigned *idx,const struct tpacket_req *treq){
	ssize_t inc = treq->tp_frame_size; // advance at least this much
	unsigned fperb = treq->tp_block_size / treq->tp_frame_size;

	++*idx;
	if(*idx == treq->tp_frame_nr){
		inc -= (fperb - 1) * inc;
		if(treq->tp_block_nr > 1){
			inc -= (treq->tp_block_nr - 1) * treq->tp_block_size;
		}
	}else if(*idx % fperb == 0){
		inc += treq->tp_block_size - fperb * inc;
	}
	return inc;
}

static inline
void ring_packet_loop(unsigned count,int rfd,void *rxm,const struct tpacket_req *treq){
	unsigned idx = 0;

	if(count){
		while(count--){
			handle_ring_packet(rfd,rxm);
			rxm += inclen(&idx,treq);
		}
	}else for( ; ; ){
		handle_ring_packet(rfd,rxm);
		rxm += inclen(&idx,treq);
	}
}

static inline int
handle_packet_socket(const omphalos_ctx *pctx){
	struct tpacket_req rtpr;
	void *rxm;
	size_t rs;
	int rfd;

	if((rfd = packet_socket(ETH_P_ALL)) < 0){
		return -1;
	}
	if((rs = mmap_rx_psocket(rfd,&rxm,&rtpr)) == 0){
		close(rfd);
		return -1;
	}
	ring_packet_loop(pctx->count,rfd,rxm,&rtpr);
	if(unmap_psocket(rxm,rs)){
		close(rfd);
		return -1;
	}
	if(close(rfd)){
		fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",rfd,strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc,char * const *argv){
	int opt;
	omphalos_ctx pctx = {
		.pcapfn = NULL,
		.count = 0,
	};
	
	opterr = 0; // suppress getopt() diagnostic to stderr while((opt = getopt(argc,argv,":c:f:")) >= 0){ switch(opt){ case 'c':{
	while((opt = getopt(argc,argv,":c:f:")) >= 0){
		switch(opt){
		case 'c':{
			char *ep;

			if(pctx.count){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			if((pctx.count = strtoul(optarg,&ep,0)) == ULONG_MAX && errno == ERANGE){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],EXIT_FAILURE);
			}
			if(ep == optarg || *ep){
				fprintf(stderr,"Bad value for %c: %s\n",opt,optarg);
				usage(argv[0],EXIT_FAILURE);
			}
			break;
		}case 'f':{
			if(pctx.pcapfn){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx.pcapfn = optarg;
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
	if(pctx.pcapfn){
		if(handle_pcap_file(&pctx)){
			return EXIT_FAILURE;
		}
	}else{
		if(handle_packet_socket(&pctx)){
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
