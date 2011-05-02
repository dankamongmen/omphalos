#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <linux/if_packet.h>
#include <omphalos/psocket.h>
#include <omphalos/interface.h>

/* The remainder of this file is pretty omphalos-specific. It doesn't
 * belong here if this ever becomes a library. */
#include <signal.h>
#include <pthread.h>
#include <sys/poll.h>
#include <omphalos/privs.h>
#include <omphalos/netlink.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>

#ifndef PACKET_TX_RING
#define PACKET_TX_RING 13
#endif

static const unsigned MAX_FRAME_SIZE = 1518; // FIXME get from device
static const unsigned MMAP_BLOCK_COUNT = 32768; // FIXME do better

// See packet(7) and Documentation/networking/packet_mmap.txt
int packet_socket(unsigned protocol){
	int fd;

	if((fd = socket(AF_PACKET,SOCK_RAW,be16toh(protocol))) < 0){
		fprintf(stderr,"Couldn't open packet socket (%s?)\n",strerror(errno));
		return -1;
	}
	return fd;
}

static int
get_block_size(unsigned fsize,unsigned *bsize){
	int b;

	// Ought be a power of two for performance. Must be a multiple of
	// page size. Ought be a multiple of tp_frame_size for efficiency.
	b = getpagesize();
	if(b < 0){
		fprintf(stderr,"Couldn't get page size (%s?)\n",strerror(errno));
		return -1;
	}
	*bsize = b;
	while(*bsize < fsize){
		if((*bsize << 1u) < *bsize){
			fprintf(stderr,"No valid configurations found\n");
			return -1;
		}
		*bsize <<= 1u;
	}
	return 0;
}

// Returns 0 on failure, otherwise size of the ringbuffer. On a failure,
// contents of treq are unspecified.
static size_t
size_mmap_psocket(struct tpacket_req *treq){
	// Must be a multiple of TPACKET_ALIGNMENT, and the following must
	// hold: TPACKET_HDRLEN <= tp_frame_size <= tp_block_size.
	treq->tp_frame_size = TPACKET_ALIGN(TPACKET_HDRLEN + MAX_FRAME_SIZE);
	if(get_block_size(treq->tp_frame_size,&treq->tp_block_size) < 0){
		return 0;
	}
	// Array of pointers to blocks, allocated via slab -- cannot be
	// larger than largest slabbable allocation.
	treq->tp_block_nr = MMAP_BLOCK_COUNT;
	// tp_frame_nr is derived from the other three parameters.
	treq->tp_frame_nr = (treq->tp_block_size / treq->tp_frame_size)
		* treq->tp_block_nr;
	return treq->tp_block_nr * treq->tp_block_size;
}

size_t mmap_psocket(int op,int idx,int fd,void **map,struct tpacket_req *treq){
	size_t size;

	*map = MAP_FAILED;
	if((size = size_mmap_psocket(treq)) == 0){
		return 0;
	}
	if(setsockopt(fd,SOL_PACKET,op,treq,sizeof(*treq)) < 0){
		fprintf(stderr,"Couldn't set socket option (%s?)\n",strerror(errno));
		return 0;
	}
	if(op == PACKET_TX_RING){
		struct sockaddr_ll sll;

		memset(&sll,0,sizeof(sll));
		sll.sll_family = AF_PACKET;
		sll.sll_protocol = ETH_P_ALL;
		sll.sll_ifindex = idx;
		if(bind(fd,(struct sockaddr *)&sll,sizeof(sll)) < 0){
			fprintf(stderr,"Couldn't bind idx %d (%s?)\n",idx,strerror(errno));
			return 0;
		}
	}else if(idx != -1){
		fprintf(stderr,"Invalid idx with op %d: %d\n",op,idx);
		return -1;
	}
	if((*map = mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)) == MAP_FAILED){
		fprintf(stderr,"Couldn't mmap %zub (%s?)\n",size,strerror(errno));
		return 0;
	}
	// FIXME MADV_HUGEPAGE support was dropped in 2.6.38.4, it seems.
#ifdef MADV_HUGEPAGE
	if(madvise(*map,size,MADV_HUGEPAGE)){
		//fprintf(stderr,"Couldn't advise hugepages for %zu (%s?)\n",size,strerror(errno));
	}
#endif
	return size;
}

size_t mmap_rx_psocket(int fd,void **map,struct tpacket_req *treq){
	return mmap_psocket(PACKET_RX_RING,-1,fd,map,treq);
}

size_t mmap_tx_psocket(int fd,int idx,void **map,struct tpacket_req *treq){
	return mmap_psocket(PACKET_TX_RING,idx,fd,map,treq);
}

int unmap_psocket(void *map,size_t size){
	if(munmap(map,size)){
		fprintf(stderr,"Couldn't unmap %zub ring buffer (%s?)\n",size,strerror(errno));
		return -1;
	}
	return 0;
}

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
	if(discover_routes(pfd[0].fd) < 0){
		close(pfd[0].fd);
		return NULL;
	}
	if(discover_neighbors(pfd[0].fd) < 0){
		close(pfd[0].fd);
		return NULL;
	}
	strcpy(ntmarsh->errbuf,"");
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	for(;;){
		unsigned z;

		errno = 0;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) <= 0){
			fprintf(stderr,"Wakeup on netlink socket %d (%s?)\n",
					pfd[0].fd,errno ? strerror(errno) : "spontaneous");
			errno = 0;
			// FIXME bail on terrible errors?
		}
		for(z = 0 ; z < sizeof(pfd) / sizeof(*pfd) ; ++z){
			if(pfd[z].revents & POLLERR){
				snprintf(ntmarsh->errbuf,sizeof(ntmarsh->errbuf),
					"Error polling netlink socket %d\n",pfd[z].fd);
				break; // FIXME
			}else if(pfd[z].revents){
				handle_netlink_event(pfd[z].fd);
			}
			pfd[z].revents = 0;
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

static void
handle_ring_packet(int fd,void *frame){
	struct tpacket_hdr *thdr = frame;
	const struct sockaddr_ll *sall;
	interface *iface;

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
	sall = (struct sockaddr_ll *)((char *)frame + TPACKET_ALIGN(sizeof(*thdr)));
	if((iface = iface_by_idx(sall->sll_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",sall->sll_ifindex);
		return;
	}
	if((thdr->tp_status & TP_STATUS_COPY) || thdr->tp_snaplen != thdr->tp_len){
		fprintf(stderr,"Partial capture on %s (%d) (%u/%ub)\n",
				iface->name,sall->sll_ifindex,thdr->tp_snaplen,thdr->tp_len);
		++iface->truncated;
		return;
	}
	if(thdr->tp_status & TP_STATUS_LOSING){
		fprintf(stderr,"FUCK ME; THE RINGBUFFER'S FULL!\n");
	}
	++iface->frames;
	handle_ethernet_packet(iface,(char *)frame + thdr->tp_mac,thdr->tp_len);
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

int handle_packet_socket(const omphalos_ctx *pctx){
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
