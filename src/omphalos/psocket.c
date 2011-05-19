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

static const unsigned MMAP_BLOCK_COUNT = 8192; // FIXME do better

// See packet(7) and Documentation/networking/packet_mmap.txt
int packet_socket(const omphalos_iface *pctx,unsigned protocol){
	int fd;

	if((fd = socket(AF_PACKET,SOCK_RAW,ntohs(protocol))) < 0){
		pctx->diagnostic("Couldn't open packet socket (%s?)",strerror(errno));
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
size_mmap_psocket(struct tpacket_req *treq,unsigned maxframe){
	// Must be a multiple of TPACKET_ALIGNMENT, and the following must
	// hold: TPACKET_HDRLEN <= tp_frame_size <= tp_block_size.
	treq->tp_frame_size = TPACKET_ALIGN(TPACKET_HDRLEN + sizeof(struct tpacket_hdr) + maxframe);
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

static size_t
mmap_psocket(const omphalos_iface *octx,int op,int idx,int fd,
			unsigned maxframe,void **map,struct tpacket_req *treq){
	size_t size;

	*map = MAP_FAILED;
	if((size = size_mmap_psocket(treq,maxframe)) == 0){
		return 0;
	}
	if(idx >= 0){
		struct sockaddr_ll sll;

		memset(&sll,0,sizeof(sll));
		sll.sll_family = AF_PACKET;
		sll.sll_ifindex = idx;
		if(bind(fd,(struct sockaddr *)&sll,sizeof(sll)) < 0){
			octx->diagnostic("Couldn't bind idx %d (%s?)",idx,strerror(errno));
			return 0;
		}
	}else if(op == PACKET_TX_RING){
		octx->diagnostic("Invalid idx with op %d: %d",op,idx);
		return -1;
	}
	if(setsockopt(fd,SOL_PACKET,op,treq,sizeof(*treq)) < 0){
		octx->diagnostic("Couldn't set socket option (%s?)",strerror(errno));
		return 0;
	}
	if((*map = mmap(0,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)) == MAP_FAILED){
		octx->diagnostic("Couldn't mmap %zub (%s?)",size,strerror(errno));
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

size_t mmap_tx_psocket(const omphalos_iface *octx,int fd,int idx,
				unsigned maxframe,void **map,
				struct tpacket_req *treq){
	return mmap_psocket(octx,PACKET_TX_RING,idx,fd,maxframe,map,treq);
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

static int
restore_sighandler(const omphalos_iface *pctx){
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_ONSTACK | SA_RESTART,
	};

	if(sigaction(SIGINT,&sa,NULL)){
		pctx->diagnostic("Couldn't restore sighandler (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

static int
setup_sighandler(const omphalos_iface *octx){
	struct sigaction sa = {
		.sa_handler = cancellation_signal_handler,
		.sa_flags = SA_ONSTACK | SA_RESTART,
	};
	sigset_t csigs;

	if(sigemptyset(&csigs) || sigaddset(&csigs,SIGINT)){
		octx->diagnostic("Couldn't prepare sigset (%s?)",strerror(errno));
		return -1;
	}
	if(sigaction(SIGINT,&sa,NULL)){
		octx->diagnostic("Couldn't install sighandler (%s?)",strerror(errno));
		return -1;
	}
	if(pthread_sigmask(SIG_UNBLOCK,&csigs,NULL)){
		octx->diagnostic("Couldn't unmask signals (%s?)",strerror(errno));
		restore_sighandler(octx);
		return -1;
	}
	return 0;
}
// End nasty signals-based cancellation.

typedef struct netlink_thread_marshal {
	const omphalos_iface *octx;
} netlink_thread_marshal;

static int 
netlink_thread(const omphalos_iface *octx){
	struct pollfd pfd[1] = {
		{
			.events = POLLIN | POLLRDNORM | POLLERR,
		}
	};
	int events;

	if((pfd[0].fd = netlink_socket()) < 0){
		return -1;
	}
	if(discover_links(octx,pfd[0].fd) < 0){
		close(pfd[0].fd);
		return -1;
	}
	if(discover_routes(octx,pfd[0].fd) < 0){
		close(pfd[0].fd);
		return -1;
	}
	if(discover_neighbors(octx,pfd[0].fd) < 0){
		close(pfd[0].fd);
		return -1;
	}
	for(;;){
		unsigned z;

		errno = 0;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) == 0){
			octx->diagnostic("Spontaneous wakeup on netlink socket %d",pfd[0].fd);
		}
		if(events < 0){
			octx->diagnostic("Error polling netlink socket %d (%s?)",
					pfd[0].fd,strerror(errno));
			break;
		}
		for(z = 0 ; z < sizeof(pfd) / sizeof(*pfd) ; ++z){
			if(pfd[z].revents & POLLERR){
				octx->diagnostic("Error polling netlink socket %d\n",pfd[z].fd);
			}else if(pfd[z].revents){
				handle_netlink_event(octx,pfd[z].fd);
			}
			pfd[z].revents = 0;
		}
	}
	// FIXME reap packet socket threads...
	close(pfd[0].fd);
	return 0;
}

static void
handle_ring_packet(const omphalos_iface *octx,interface *iface,int fd,void *frame){
	struct tpacket_hdr *thdr = frame;
	const struct sockaddr_ll *sall;

	while(thdr->tp_status == 0){
		struct pollfd pfd[1];
		int events;

		pfd[0].fd = fd;
		pfd[0].revents = 0;
		pfd[0].events = POLLIN | POLLRDNORM | POLLERR;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) == 0){
			octx->diagnostic("Interrupted polling packet socket %d",fd);
		}
		if(events < 0){
			if(errno != EINTR){
				octx->diagnostic("Error polling packet socket %d (%s?)",fd,strerror(errno));
				pthread_exit(NULL);
			}
		}else if(pfd[0].revents & POLLERR){
			octx->diagnostic("Error polling packet socket %d",fd);
			pthread_exit(NULL);
		}
	}
	sall = (struct sockaddr_ll *)((char *)frame + TPACKET_ALIGN(sizeof(*thdr)));
	if((thdr->tp_status & TP_STATUS_COPY) || thdr->tp_snaplen != thdr->tp_len){
		octx->diagnostic("Partial capture on %s (%d) (%u/%ub)",
				iface->name,sall->sll_ifindex,thdr->tp_snaplen,thdr->tp_len);
		++iface->truncated;
		thdr->tp_status = TP_STATUS_KERNEL; // return the frame
		return;
	}
	if(thdr->tp_status & TP_STATUS_LOSING){
		octx->diagnostic("FUCK ME; THE RINGBUFFER'S FULL!");
	}
	++iface->frames;
	handle_ethernet_packet(octx,iface,(char *)frame + thdr->tp_mac,thdr->tp_len);
	if(octx->packet_read){
		octx->packet_read(iface,iface->opaque);
	}
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

int ring_packet_loop(const omphalos_iface *octx,interface *i,int rfd,
			void *rxm,const struct tpacket_req *treq){
	unsigned idx = 0;

	while(!cancelled){
		handle_ring_packet(octx,i,rfd,rxm);
		rxm += inclen(&idx,treq);
	}
	return 0;
}

size_t mmap_rx_psocket(const omphalos_iface *octx,int fd,int idx,
		unsigned maxframe,void **map,struct tpacket_req *treq){
	return mmap_psocket(octx,PACKET_RX_RING,idx,fd,maxframe,map,treq);
}

int handle_packet_socket(const omphalos_ctx *pctx){
	int ret;

	if(setup_sighandler(&pctx->iface)){
		return -1;
	}
	ret = netlink_thread(&pctx->iface);
	ret |= restore_sighandler(&pctx->iface);
	return ret;
}
