#include <pcap.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <omphalos/ll.h>
#include <linux/if_arp.h>
#include <net/ethernet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_packet.h>
#include <omphalos/netlink.h>
#include <omphalos/psocket.h>

#define MAXINTERFACES (1u << 16) // lame FIXME

typedef struct omphalos_ctx {
	const char *pcapfn;
	unsigned long count;
} omphalos_ctx;

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -f filename ] [ -c count ]\n",arg0);
	exit(ret);
}

typedef struct interface {
	unsigned long pkts;
	unsigned arptype;
	char name[IFNAMSIZ];
	int mtu;		// to match netdevice(7)'s ifr_mtu...
} interface;

static interface interfaces[MAXINTERFACES];

// we wouldn't naturally want to use signed integers, but that's the api...
static inline interface *
iface_by_idx(int idx){
	if(idx < 0 || (unsigned)idx >= sizeof(interfaces) / sizeof(*interfaces)){
		return NULL;
	}
	return &interfaces[idx];
}

// len is actual packet data length, not packet_mmap leaders
static void
handle_packet(const struct timeval *tv,const void *frame,size_t len){
	const struct sockaddr_ll *sall = frame;
	interface *iface;

	if((iface = iface_by_idx(sall->sll_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",sall->sll_ifindex);
		return;
	}
	++iface->pkts;
	if(len <= 99999){
		printf("[%d][%5zub] %lu.%06lu\n",sall->sll_ifindex,len,tv->tv_sec,tv->tv_usec);
	}else{
		printf("[%d][%zub] %lu.%06lu\n",sall->sll_ifindex,len,tv->tv_sec,tv->tv_usec);
	}
}

typedef struct arptype {
	unsigned ifi_type;
	const char *name;
} arptype;

static arptype arptypes[] = {
	{
		.ifi_type = ARPHRD_LOOPBACK,
		.name = "loopback",
	},{
		.ifi_type = ARPHRD_ETHER,
		.name = "Ethernet",
	},{
		.ifi_type = ARPHRD_IEEE80211,
		.name = "IEEE 802.11",
	},
};

static inline const arptype *
lookup_arptype(unsigned arphrd){
	unsigned idx;

	for(idx = 0 ; idx < sizeof(arptypes) / sizeof(*arptypes) ; ++idx){
		const arptype *at = arptypes + idx;

		if(at->ifi_type == arphrd){
			return at;
		}
	}
	return NULL;
}

#define IFF_FLAG(flags,f) ((flags) & (IFF_##f) ? #f" " : "")
static int
handle_rtm_newlink(const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	const struct rtattr *ra;
	const arptype *at;
	interface *iface;
	int rlen;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ii->ifi_index);
		return -1;
	}
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*ii));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*ii));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
			case IFLA_ADDRESS:{
				break;
			}case IFLA_BROADCAST:{
				break;
			}case IFLA_IFNAME:{
				if(strlen(RTA_DATA(ra)) >= sizeof(iface->name)){
					fprintf(stderr,"Name too long: %s\n",(char *)RTA_DATA(ra));
					return -1;
				}
				strcpy(iface->name,RTA_DATA(ra));
				break;
			}case IFLA_MTU:{
				iface->mtu = *(int *)RTA_DATA(ra);
				break;
			}case IFLA_LINK:{
				break;
			}case IFLA_TXQLEN:{
				break;
			}case IFLA_MAP:{
				break;
			}case IFLA_WEIGHT:{
				break;
			}case IFLA_QDISC:{
				break;
			}case IFLA_STATS:{
				break;
			}case IFLA_WIRELESS:{
				break;
			}case IFLA_OPERSTATE:{
				break;
			}case IFLA_LINKMODE:{
				break;
			}case IFLA_LINKINFO:{
				break;
			}case IFLA_NET_NS_PID:{
				break;
			}case IFLA_IFALIAS:{
				break;
			}case IFLA_NUM_VF:{
				break;
			}case IFLA_VFINFO_LIST:{
				break;
			}case IFLA_STATS64:{
				break;
			}case IFLA_VF_PORTS:{
				break;
			}case IFLA_PORT_SELF:{
				break;
			}case IFLA_AF_SPEC:{
				break;
			}default:{
				fprintf(stderr,"Unknown rtatype %u\n",ra->rta_type);
				break;
			}
		}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	iface->arptype = ii->ifi_type;
	if((at = lookup_arptype(iface->arptype)) == NULL){
		fprintf(stderr,"Unknown dev type %u\n",iface->arptype);
	}else{
		printf("[%3d][%8s][%s] mtu: %d %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			ii->ifi_index,
			iface->name,
			at->name,
			iface->mtu,
			IFF_FLAG(ii->ifi_flags,UP),
			IFF_FLAG(ii->ifi_flags,BROADCAST),
			IFF_FLAG(ii->ifi_flags,DEBUG),
			IFF_FLAG(ii->ifi_flags,LOOPBACK),
			IFF_FLAG(ii->ifi_flags,POINTOPOINT),
			IFF_FLAG(ii->ifi_flags,NOTRAILERS),
			IFF_FLAG(ii->ifi_flags,RUNNING),
			IFF_FLAG(ii->ifi_flags,PROMISC),
			IFF_FLAG(ii->ifi_flags,ALLMULTI),
			IFF_FLAG(ii->ifi_flags,MASTER),
			IFF_FLAG(ii->ifi_flags,SLAVE),
			IFF_FLAG(ii->ifi_flags,MULTICAST),
			IFF_FLAG(ii->ifi_flags,PORTSEL),
			IFF_FLAG(ii->ifi_flags,AUTOMEDIA),
			IFF_FLAG(ii->ifi_flags,DYNAMIC),
			IFF_FLAG(ii->ifi_flags,LOWER_UP),
			IFF_FLAG(ii->ifi_flags,DORMANT),
			IFF_FLAG(ii->ifi_flags,ECHO)
			);
	}
	return 0;
}
#undef IFF_FLAG

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
	while((r = recvmsg(fd,&msg,MSG_DONTWAIT)) > 0){
		// NLMSG_LENGTH sanity checks enforced via NLMSG_OK() and
		// _NEXT() -- we needn't check amount read within the loop
		for(nh = (struct nlmsghdr *)buf ; NLMSG_OK(nh,(unsigned)r) ; nh = NLMSG_NEXT(nh,r)){
			if(nh->nlmsg_flags & NLM_F_MULTI){
				inmulti = 1;
			}
			switch(nh->nlmsg_type){
			case RTM_NEWLINK:{
				handle_rtm_newlink(nh);
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
	if(r < 0 && errno != EAGAIN){
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
handle_ring_packet(int fd,void *frame){
	struct tpacket_hdr *thdr = frame;
	struct timeval tv;

	while(thdr->tp_status == 0){
		struct pollfd pfd[1];
		int events;

		// fprintf(stderr,"Packet not ready\n");
		pfd[0].fd = fd;
		pfd[0].revents = 0;
		pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) == 0){
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
	}
	if((thdr->tp_status & TP_STATUS_COPY) || thdr->tp_snaplen != thdr->tp_len){
		fprintf(stderr,"Partial capture (%u/%ub)\n",thdr->tp_snaplen,thdr->tp_len);
		return;
	}
	if(thdr->tp_status & TP_STATUS_LOSING){
		fprintf(stderr,"FUCK ME; THE RINGBUFFER'S FULL!\n");
	}
	tv.tv_sec = thdr->tp_sec;
	tv.tv_usec = thdr->tp_usec;
	handle_packet(&tv,(const char *)frame + TPACKET_ALIGN(sizeof(*thdr)),thdr->tp_len);
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

	if(count){
		while(count--){
			handle_ring_packet(rfd,rxm);
			rxm += inclen(&idx,treq);
		}
	}else for( ; ; ){ // FIXME install signal handler
		handle_ring_packet(rfd,rxm);
		rxm += inclen(&idx,treq);
	}
	return 0;
}

typedef struct netlink_thread_marshal {
	int fd;
	char errbuf[256];
} netlink_thread_marshal;

static void *
netlink_thread(void *v){
	netlink_thread_marshal *ntmarsh = v;
	struct pollfd pfd[1] = {
		{
			.events = POLLIN | POLLRDNORM | POLLERR,
			.fd = ntmarsh->fd,
		}
	};
	int events;

	strcpy(ntmarsh->errbuf,"");
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	for(;;){
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) == 0){
			fprintf(stderr,"Interrupted polling netlink socket %d\n",ntmarsh->fd);
		}
		if(pfd[0].revents & POLLERR){
			snprintf(ntmarsh->errbuf,sizeof(ntmarsh->errbuf),"Error polling netlink socket %d\n",ntmarsh->fd);
			break; // FIXME
		}else if(pfd[0].revents){
			handle_netlink_event(pfd[0].fd);
		}
	}
	pthread_exit(ntmarsh->errbuf);
}

static int
reap_netlink_thread(pthread_t tid){
	void *ret;

	if( (errno = pthread_cancel(tid)) ){
		fprintf(stderr,"Couldn't cancel netlink thread (%s?)\n",strerror(errno));
	}
	if( (errno = pthread_join(tid,&ret)) ){
		fprintf(stderr,"Couldn't join netlink thread (%s?)\n",strerror(errno));
		return -1;
	}
	if(ret != PTHREAD_CANCELED){
		fprintf(stderr,"Netlink thread returned error on exit (%s)\n",(char *)ret);
		return -1;
	}
	printf("Successfully reaped netlink thread\n");
	return 0;
}

static inline int
handle_packet_socket(const omphalos_ctx *pctx){
	netlink_thread_marshal ntmarsh;
	struct tpacket_req rtpr;
	int rfd,ret = 0;
	pthread_t nltid;
	void *rxm;
	size_t rs;
	int nfd;

	// FIXME move into netlink thread
	if((nfd = netlink_socket()) < 0){
		return -1;
	}
	if(discover_links(nfd) < 0){
		close(nfd);
		return -1;
	}
	ntmarsh.fd = nfd;
	if( (errno = pthread_create(&nltid,NULL,netlink_thread,&ntmarsh)) ){
		fprintf(stderr,"Couldn't create netlink thread (%s?)\n",strerror(errno));
		close(nfd);
		return -1;
	}
	if((rfd = packet_socket(ETH_P_ALL)) < 0){
		reap_netlink_thread(nltid);
		return -1;
	}
	if((rs = mmap_rx_psocket(rfd,&rxm,&rtpr)) == 0){
		close(rfd);
		reap_netlink_thread(nltid);
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
	ret |= reap_netlink_thread(nltid);
	ret |= close(nfd); // FIXME netlink thread, netlink thread!
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
