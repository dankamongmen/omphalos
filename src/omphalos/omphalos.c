#include <pwd.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/uio.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/sll.h>
#include <net/ethernet.h>
#include <omphalos/pcap.h>
#include <linux/netlink.h>
#include <sys/capability.h>
#include <linux/rtnetlink.h>
#include <omphalos/netlink.h>
#include <omphalos/psocket.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#define MAXINTERFACES (1u << 16) // lame FIXME
#define DEFAULT_USERNAME "nobody"

// External cancellation, tested in input-handling loops. This only works
// without a mutex lock (memory barrier, more precisely) because we
// restrict signal handling to the input-handling threads (via initial
// setprocmask() followed by pthread_sigmask() in these threads only). 
static volatile unsigned cancelled;

static void
cancellation_signal_handler(int signo __attribute__ ((unused))){
	cancelled = 1;
}
// End nasty signals-based cancellation.

static void
usage(const char *arg0,int ret){
	fprintf(stderr,"usage: %s [ -u username ] [ -f filename ] [ -c count ]\n",
			basename(arg0));
	fprintf(stderr,"options:\n");
	fprintf(stderr,"-u username: user name to take after creating packet socket.\n");
	fprintf(stderr,"\t'%s' by default. provide empty string to disable.\n",DEFAULT_USERNAME);
	fprintf(stderr,"-f filename: libpcap-format save file for input.\n");
	fprintf(stderr,"-c count: exit after reading this many packets.\n");
	exit(ret);
}

static interface interfaces[MAXINTERFACES];

// we wouldn't naturally want to use signed integers, but that's the api...
static inline interface *
iface_by_idx(int idx){
	if(idx < 0 || (unsigned)idx >= sizeof(interfaces) / sizeof(*interfaces)){
		return NULL;
	}
	return &interfaces[idx];
}

// len is actual packet data length, not mmap (tpacket_hdr etc) leaders
static void
handle_live_packet(const void *frame,size_t len){
	const struct sockaddr_ll *sall = frame;
	interface *iface;

	if((iface = iface_by_idx(sall->sll_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",sall->sll_ifindex);
		return;
	}
	++iface->pkts;
	handle_ethernet_packet((char *)frame + sizeof(*sall),len - sizeof(*sall));
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

static int
handle_rtm_newneigh(const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",nd->ndm_ifindex);
		return -1;
	}
	printf("[%s] NEIGHBOR ADDED\n",iface->name);
	// FIXME
	return 0;
}

static int
handle_rtm_delneigh(const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",nd->ndm_ifindex);
		return -1;
	}
	printf("[%s] NEIGHBOR DELETED\n",iface->name);
	// FIXME
	return 0;
}

static int
handle_rtm_deladdr(const struct nlmsghdr *nl){
	const struct ifaddrmsg *ia = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ia->ifa_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ia->ifa_index);
		return -1;
	}
	printf("[%s] ADDRESS DELETED\n",iface->name);
	// FIXME
	return 0;
}

static int
handle_rtm_newaddr(const struct nlmsghdr *nl){
	const struct ifaddrmsg *ia = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ia->ifa_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ia->ifa_index);
		return -1;
	}
	printf("[%s] ADDRESS ADDED\n",iface->name);
	// FIXME
	return 0;
}

static int
handle_rtm_dellink(const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ii->ifi_index);
		return -1;
	}
	printf("Link %d (%s) was removed\n",ii->ifi_index,iface->name);
	// FIXME do we care?
	return 0;
}

static int
handle_wireless_event(interface *i,void *data,int len){
	fprintf(stderr,"WIRELESS EVENT on %s (%p/%d)!\n",i->name,data,len);
	return 0;
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
				char *addr;

				if((addr = malloc(RTA_PAYLOAD(ra))) == NULL){
					fprintf(stderr,"Address too long: %lu\n",RTA_PAYLOAD(ra));
					return -1;
				}
				memcpy(addr,RTA_DATA(ra),RTA_PAYLOAD(ra));
				free(iface->addr);
				iface->addr = addr;
				iface->addrlen = RTA_PAYLOAD(ra);
				break;
			}case IFLA_BROADCAST:{
				break;
			}case IFLA_IFNAME:{
				char *name;

				if((name = strdup(RTA_DATA(ra))) == NULL){
					fprintf(stderr,"Name too long: %s\n",(char *)RTA_DATA(ra));
					return -1;
				}
				free(iface->name);
				iface->name = name;
				break;
			}case IFLA_MTU:{
				if(RTA_PAYLOAD(ra) != sizeof(int)){
					fprintf(stderr,"Expected %zu MTU bytes, got %lu\n",
							sizeof(int),RTA_PAYLOAD(ra));
				}
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
				if(handle_wireless_event(iface,RTA_DATA(ra),RTA_PAYLOAD(ra)) < 0){
					return -1;
				}
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
		char *hwaddr;

		if((hwaddr = hwaddrstr(iface)) == NULL){
			return -1;
		}
		printf("[%3d][%8s][%s] %s %d %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			ii->ifi_index,
			iface->name,
			at->name,
			hwaddr,
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
		free(hwaddr);
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
	int r,inmulti,res;

	res = 0;
	// For handling multipart messages
	inmulti = 0;
	while((r = recvmsg(fd,&msg,MSG_DONTWAIT)) > 0){
		// NLMSG_LENGTH sanity checks enforced via NLMSG_OK() and
		// _NEXT() -- we needn't check amount read within the loop
		for(nh = (struct nlmsghdr *)buf ; NLMSG_OK(nh,(unsigned)r) ; nh = NLMSG_NEXT(nh,r)){
			//printf("MSG TYPE %d\n",(int)nh->nlmsg_type);
			if(nh->nlmsg_flags & NLM_F_MULTI){
				inmulti = 1;
			}
			switch(nh->nlmsg_type){
			case RTM_NEWLINK:{
				res |= handle_rtm_newlink(nh);
				break;
			}case RTM_DELLINK:{
				res |= handle_rtm_dellink(nh);
				break;
			}case RTM_NEWADDR:{
				res |= handle_rtm_newaddr(nh);
				break;
			}case RTM_DELADDR:{
				res |= handle_rtm_deladdr(nh);
				break;
			}case RTM_NEWNEIGH:{
				res |= handle_rtm_newneigh(nh);
				break;
			}case RTM_DELNEIGH:{
				res |= handle_rtm_delneigh(nh);
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
					res = -1;
				}
				break;
			}default:{
				fprintf(stderr,"Unknown netlink msgtype %u on %d\n",nh->nlmsg_type,fd);
				res = -1;
			}}
			// FIXME handle read data
		}
	}
	if(inmulti){
		fprintf(stderr,"Warning: unterminated multipart on %d\n",fd);
		res = -1;
	}
	if(r < 0 && errno != EAGAIN){
		fprintf(stderr,"Error reading netlink socket %d (%s?)\n",
				fd,strerror(errno));
		res = -1;
	}else if(r == 0){
		fprintf(stderr,"EOF on netlink socket %d\n",fd);
		// FIXME reopen...?
		res = -1;
	}
	return res;
}

static void
handle_ring_packet(int fd,void *frame){
	struct tpacket_hdr *thdr = frame;

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
			if(!cancelled || errno != EINTR){
				fprintf(stderr,"Error polling packet socket %d (%s?)\n",fd,strerror(errno));
			}
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
	handle_live_packet((const char *)frame + TPACKET_ALIGN(sizeof(*thdr)),thdr->tp_len);
	thdr->tp_status = TP_STATUS_KERNEL; // return the frame
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
	}else{
		while(!cancelled){
			handle_ring_packet(rfd,rxm);
			rxm += inclen(&idx,treq);
		}
	}
	return 0;
}

typedef struct netlink_thread_marshal {
	char errbuf[256];
} netlink_thread_marshal;

static void *
netlink_thread(void *v){
	netlink_thread_marshal *ntmarsh = v;
	struct pollfd pfd[1] = {
		{
			.events = POLLIN | POLLRDNORM | POLLERR,
		}
	};
	int events;

	// FIXME how do we ensure this is closed after we get cancelled?
	if((pfd[0].fd = netlink_socket()) < 0){
		return NULL;
	}
	if(discover_links(pfd[0].fd) < 0){
		close(pfd[0].fd);
		return NULL;
	}
	strcpy(ntmarsh->errbuf,"");
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	for(;;){
		errno = 0;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) <= 0){
			fprintf(stderr,"Wakeup on netlink socket %d (%s?)\n",
					pfd[0].fd,errno ? strerror(errno) : "spontaneous");
			errno = 0;
			// FIXME bail on terrible errors?
		}
		if(pfd[0].revents & POLLERR){
			snprintf(ntmarsh->errbuf,sizeof(ntmarsh->errbuf),"Error polling netlink socket %d\n",pfd[0].fd);
			break; // FIXME
		}else if(pfd[0].revents){
			handle_netlink_event(pfd[0].fd);
		}
		pfd[0].revents = 0;
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
	return 0;
}

static int
handle_priv_drop(const char *name){
	cap_flag_value_t val;
	cap_t cap;

	if(strlen(name) == 0){ // empty string disables permissions drop
		return 0;
	}
	if((cap = cap_get_pid(getpid())) == NULL){
		return -1;
	}
	if(cap_get_flag(cap,CAP_SETUID,CAP_EFFECTIVE,&val)){
		cap_free(cap);
		return -1;
	}
	cap_free(cap);
	if(val == CAP_SET){
		struct passwd *pw = getpwnam(name);

		if(pw == NULL){
			return -1;
		}
		if(setuid(pw->pw_uid)){
			return -1;
		}
	}
	return 0;
}

static int
mask_cancel_sigs(sigset_t *oldsigs){
	struct sigaction sa = {
		.sa_handler = cancellation_signal_handler,
		.sa_flags = SA_ONSTACK | SA_RESTART,
	};
	sigset_t cancelsigs;

	if(sigemptyset(&cancelsigs) || sigaddset(&cancelsigs,SIGINT)){
		fprintf(stderr,"Couldn't prep signals (%s?)\n",strerror(errno));
		return -1;
	}
	if(sigprocmask(SIG_BLOCK,&cancelsigs,oldsigs)){
		fprintf(stderr,"Couldn't mask signals (%s?)\n",strerror(errno));
		return -1;
	}
	if(sigaction(SIGINT,&sa,NULL)){
		fprintf(stderr,"Couldn't install sighandler (%s?)\n",strerror(errno));
		return -1;
	}
	return 0;
}

static int
handle_packet_socket(const omphalos_ctx *pctx){
	netlink_thread_marshal ntmarsh;
	struct tpacket_req rtpr;
	sigset_t oldsigs;
	pthread_t nltid;
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
	if(handle_priv_drop(pctx->user)){
		fprintf(stderr,"Couldn't become user %s (%s?)\n",pctx->user,strerror(errno));
		unmap_psocket(rxm,rs);
		close(rfd);
		return -1;
	}
	// Before we create other threads, mask cancellation signals. We
	// only want signals to be handled on the main input threads, so
	// that we can locklessly test for cancellation.
	if(mask_cancel_sigs(&oldsigs)){
		unmap_psocket(rxm,rs);
		close(rfd);
		return -1;
	}
	if( (errno = pthread_create(&nltid,NULL,netlink_thread,&ntmarsh)) ){
		fprintf(stderr,"Couldn't create netlink thread (%s?)\n",strerror(errno));
		unmap_psocket(rxm,rs);
		close(rfd);
		return -1;
	}
	if(pthread_sigmask(SIG_SETMASK,&oldsigs,NULL)){
		fprintf(stderr,"Couldn't unmask signals (%s?)\n",strerror(errno));
		reap_netlink_thread(nltid);
		unmap_psocket(rxm,rs);
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
	ret |= reap_netlink_thread(nltid);
	return ret;
}

static int
print_stats(FILE *fp){
	const interface *iface;
	interface total;
	unsigned i;

	memset(&total,0,sizeof(total));
	if(printf("<stats>") < 0){
		return -1;
	}
	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		iface = &interfaces[i];
		if(iface->pkts || iface->name){
			if(print_iface_stats(fp,iface,&total,"iface") < 0){
				return -1;
			}
		}
	}
	if(print_pcap_stats(fp,&total) < 0){
		return -1;
	}
	if(print_iface_stats(fp,&total,NULL,"total") < 0){
		return -1;
	}
	if(printf("</stats>\n") < 0){
		return -1;
	}
	return 0;
}

int main(int argc,char * const *argv){
	int opt;
	omphalos_ctx pctx = {
		.pcapfn = NULL,
		.count = 0,
		.user = NULL,
	};
	
	opterr = 0; // suppress getopt() diagnostic to stderr while((opt = getopt(argc,argv,":c:f:")) >= 0){ switch(opt){ case 'c':{
	while((opt = getopt(argc,argv,":c:f:u:")) >= 0){
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
			if(pctx.count == 0){
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
		}case 'u':{
			if(pctx.user){
				fprintf(stderr,"Provided %c twice\n",opt);
				usage(argv[0],EXIT_FAILURE);
			}
			pctx.user = optarg;
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
	if(pctx.user == NULL){
		pctx.user = DEFAULT_USERNAME;
	}
	if(pctx.pcapfn){
		if(handle_priv_drop(pctx.user)){
			fprintf(stderr,"Couldn't become user %s (%s?)\n",pctx.user,strerror(errno));
			usage(argv[0],EXIT_FAILURE);
		}
		if(handle_pcap_file(&pctx)){
			return EXIT_FAILURE;
		}
	}else{
		if(handle_packet_socket(&pctx)){
			return EXIT_FAILURE;
		}
	}
	if(print_stats(stdout)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
