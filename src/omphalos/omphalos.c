#include <pcap.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <omphalos/ll.h>
#include <net/ethernet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
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

static int
handle_netlink_event(int fd){
	char buf[4096]; // FIXME numerous problems
	struct iovec iov[1] = { { buf, sizeof(buf) } };
	struct sockaddr_nl sa;
	struct msghdr msg = {
		&sa,	sizeof(sa),	iov,	sizeof(iov) / sizeof(*iov), NULL, 0, 0
	};
	struct nlmsghdr *nh;
	int r,inmulti;

	// For handling multipart messages
	inmulti = 0;
	while((r = recvmsg(fd,&msg,0)) > 0){
		for(nh = (struct nlmsghdr *)buf ; NLMSG_OK(nh,(unsigned)r) ; nh = NLMSG_NEXT(nh,r)){
			if(nh->nlmsg_flags & NLM_F_MULTI){
				inmulti = 1;
			}
			switch(nh->nlmsg_type){
			case RTM_NEWLINK:{
				// FIXME handle new link
				break;
			}case NLMSG_DONE:{
				if(!inmulti){
					fprintf(stderr,"Warning: DONE outside multipart on %d\n",fd);
				}
				inmulti = 0;
				break;
			}case NLMSG_ERROR:{
				struct nlmsgerr *nerr = NLMSG_DATA(nh);

				if(nerr->error == 0){
					printf("ACK on netlink %d msgid %u type %u\n",
						fd,nerr->msg.nlmsg_seq,nerr->msg.nlmsg_type);
				}else{
					fprintf(stderr,"Error message on netlink %d msgid %u (%s?)\n",
						fd,nerr->msg.nlmsg_seq,strerror(nerr->error));
				}
				break;
			}default:{
				fprintf(stderr,"Unknown netlink msgtype %u on %d\n",nh->nlmsg_type,fd);
			}}
			// FIXME handle read data
		}
	}
	if(inmulti){
		fprintf(stderr,"Warning: unterminated multipart on %d\n",fd);
	}
	if(r < 0){
		// FIXME non-blocking handle
		fprintf(stderr,"Error reading netlink socket %d (%s?)\n",
				fd,strerror(errno));
		return -1;
	}else if(r == 0){
		fprintf(stderr,"EOF on netlink socket %d\n",fd);
		// FIXME reopen...?
		return -1;
	}
	return 0;
}

static void
handle_ring_packet(int nfd,int fd,void *frame){
	struct tpacket_hdr *thdr = frame;
	struct timeval tv;

	while(thdr->tp_status == 0){
		struct pollfd pfd[2];
		int events;

		// fprintf(stderr,"Packet not ready\n");
		pfd[0].fd = fd;
		pfd[0].revents = 0;
		pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
		pfd[1].fd = nfd;
		pfd[1].revents = 0;
		pfd[1].events = POLLIN | POLLRDNORM | POLLERR;
		while((events = poll(pfd,2,-1)) == 0){
			fprintf(stderr,"Interrupted polling packet socket %d\n",fd);
		}
		if(events < 0){
			fprintf(stderr,"Error polling packet socket %d (%s?)\n",fd,strerror(errno));
			return;
		}
		if(pfd[0].revents & POLLERR){
			fprintf(stderr,"Error polling packet socket %d\n",fd);
			return;
		}
		if(pfd[1].revents & POLLERR){
			fprintf(stderr,"Error polling netlink socket %d\n",nfd);
			return;
		}else if(pfd[1].revents){
			// FIXME handle netlink event
			// FIXME this can lead to starvation -- if we have
			// a continuous packet stream, we never read
			// netlink events
			handle_netlink_event(nfd);
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
		inc -= fperb * inc;
		if(treq->tp_block_nr > 1){
			inc -= (treq->tp_block_nr - 1) * treq->tp_block_size;
		}
		*idx = 0;
	}else if(*idx % fperb == 0){
		inc += treq->tp_block_size - fperb * inc;
	}
	return inc;
}

static inline int
ring_packet_loop(unsigned count,int rfd,void *rxm,const struct tpacket_req *treq){
	unsigned idx = 0;
	int nfd;

	if((nfd = netlink_socket()) < 0){
		return -1;
	}
	if(discover_links(nfd) < 0){
		close(nfd);
		return -1;
	}
	if(count){
		while(count--){
			handle_ring_packet(nfd,rfd,rxm);
			rxm += inclen(&idx,treq);
		}
	}else for( ; ; ){ // FIXME install signal handler
		handle_ring_packet(nfd,rfd,rxm);
		rxm += inclen(&idx,treq);
	}
	if(close(nfd)){
		fprintf(stderr,"Couldn't close netlink socket %d (%s?)\n",nfd,strerror(errno));
		return -1;
	}
	return 0;
}

static inline int
handle_packet_socket(const omphalos_ctx *pctx){
	struct tpacket_req rtpr;
	int rfd,ret = 0;
	void *rxm;
	size_t rs;

	if((rfd = packet_socket(ETH_P_ALL)) < 0){
		return -1;
	}
	if((rs = mmap_rx_psocket(rfd,&rxm,&rtpr)) == 0){
		close(rfd);
		return -1;
	}
	ret |= ring_packet_loop(pctx->count,rfd,rxm,&rtpr);
	if(unmap_psocket(rxm,rs)){
		ret = -1;
	}
	if(close(rfd)){
		fprintf(stderr,"Couldn't close packet socket %d (%s?)\n",rfd,strerror(errno));
		ret = -1;
	}
	return ret;
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
