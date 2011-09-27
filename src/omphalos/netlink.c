#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <linux/if_arp.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/version.h>
#include <omphalos/route.h>
#include <omphalos/sysfs.h>
#include <linux/rtnetlink.h>
#include <omphalos/inotify.h>
#include <omphalos/ethtool.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/psocket.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/bluetooth.h>
#include <omphalos/interface.h>

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

int netlink_socket(const omphalos_iface *octx){
	struct sockaddr_nl sa;
	int fd;

	if((fd = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE)) < 0){
		octx->diagnostic("Couldn't open NETLINK_ROUTE socket (%s?)",strerror(errno));
		return -1;
	}
	memset(&sa,0,sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTNLGRP_NOTIFY | RTNLGRP_LINK | RTNLGRP_NEIGH |
			RTNLGRP_IPV4_ROUTE | RTNLGRP_IPV6_ROUTE;
	if(bind(fd,(const struct sockaddr *)&sa,sizeof(sa))){
		octx->diagnostic("Couldn't bind NETLINK_ROUTE socket %d (%s?)",fd,strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

#define nldiscover(octx,msg,famtype,famfield) do {\
	struct { struct nlmsghdr nh ; struct famtype m ; } req = { \
		.nh = { .nlmsg_len = NLMSG_LENGTH(sizeof(req.m)), \
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP, \
			.nlmsg_type = msg, }, \
		.m = { .famfield = AF_UNSPEC, }, }; \
	int r; \
	if((r = send(fd,&req,req.nh.nlmsg_len,0)) < 0){ \
		octx->diagnostic("Failure writing " #msg " to %d (%s?)",\
				fd,strerror(errno)); \
		return -1; \
	} \
	return 0; \
}while(0)

static int
discover_addrs(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETADDR,ifaddrmsg,ifa_family);
}

static int
discover_links(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETLINK,ifinfomsg,ifi_family);
}

static int
discover_neighbors(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETNEIGH,ndmsg,ndm_family);
}

static int
discover_routes(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETROUTE,rtmsg,rtm_family);
}

int iplink_modify(const omphalos_iface *octx,int fd,int idx,unsigned flags,
					unsigned mask){
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
	} req;

	memset(&req,0,sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_index = idx;
	req.i.ifi_flags = flags;
	req.i.ifi_change = mask;
	if(send(fd,&req,req.n.nlmsg_len,0) < 0){
		octx->diagnostic("Failure writing RTM_NEWLINK to %d (%s?)",
				fd,strerror(errno));
		return -1;
	}
	return 0;
}

static int
handle_rtm_newneigh(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	char ll[IFHWADDRLEN]; // FIXME get from selected interface
	struct sockaddr_storage ssd;
	struct rtattr *ra;
	interface *iface;
	int rlen,llen;
	size_t flen;
	void *ad;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		octx->diagnostic("Invalid interface index: %d",nd->ndm_ifindex);
		return -1;
	}
	switch(nd->ndm_family){
	case AF_INET:{
		flen = sizeof(uint32_t);
		ad = &((struct sockaddr_in *)&ssd)->sin_addr;
	break;}case AF_INET6:{
		flen = sizeof(uint32_t) * 4;
		ad = &((struct sockaddr_in6 *)&ssd)->sin6_addr;
	break;}default:{
		flen = 0;
	break;} }
	if(flen == 0){
		octx->diagnostic("Unknown route family %u",nd->ndm_family);
		return -1;
	}
	llen = 0;
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*nd));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*nd));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case NDA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu nw bytes on %s, got %lu",
						flen,iface->name,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}case NDA_LLADDR:{
			llen = RTA_PAYLOAD(ra);
			if(llen){
				if(llen != sizeof(ll)){
					octx->diagnostic("Expected %zu ll bytes on %s, got %d",
						sizeof(ll),iface->name,llen);
					llen = 0;
					break;
				}
				memcpy(ll,RTA_DATA(ra),sizeof(ll));
			}
		break;}case NDA_CACHEINFO:{
		break;}case NDA_PROBES:{
		break;}default:{
			octx->diagnostic("Unknown ndatype %u on %s",ra->rta_type,iface->name);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		octx->diagnostic("%d excess bytes on %s newlink message",rlen,iface->name);
	}
	if(llen){
		if(!(nd->ndm_state & (NUD_NOARP|NUD_FAILED|NUD_INCOMPLETE))){
			struct l2host *l2;

			l2 = lookup_l2host(octx,iface,ll);
			lookup_l3host(octx,iface,l2,nd->ndm_family,ad);
		}
	}
	return 0;
}

static int
handle_rtm_delneigh(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	struct sockaddr_storage ssd;
	interface *iface;
	size_t flen;
	void *ad;
	//char ll[IFHWADDRLEN]; // FIXME get from selected interface
	struct rtattr *ra;
	int rlen;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		octx->diagnostic("Invalid interface index: %d",nd->ndm_ifindex);
		return -1;
	}
	switch(nd->ndm_family){
	case AF_INET:{
		flen = sizeof(uint32_t);
		ad = &((struct sockaddr_in *)&ssd)->sin_addr;
	break;}case AF_INET6:{
		flen = sizeof(uint32_t) * 4;
		ad = &((struct sockaddr_in6 *)&ssd)->sin6_addr;
	break;}default:{
		flen = 0;
	break;} }
	if(flen == 0){
		octx->diagnostic("Unknown route family %u",nd->ndm_family);
		return -1;
	}
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*nd));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*nd));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case NDA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu nw bytes on %s, got %lu",
						flen,iface->name,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}/*case NDA_LLADDR:{
			if(RTA_PAYLOAD(ra)){
				if(RTA_PAYLOAD(ra) != sizeof(ll)){
					octx->diagnostic("Expected %zu ll bytes on %s, got %d",
						sizeof(ll),iface->name,RTA_PAYLOAD(ra));
					llen = 0;
					break;
				}
				memcpy(ll,RTA_DATA(ra),sizeof(ll));
			}
		break;}case NDA_CACHEINFO:{
		break;}case NDA_PROBES:{
		break;}default:{
			octx->diagnostic("Unknown ndatype %u on %s",ra->rta_type,iface->name);
		break;}
		*/}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		octx->diagnostic("%d excess bytes on %s newlink message",rlen,iface->name);
	}
	return 0;
}

/*static int
handle_rtm_deladdr(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifaddrmsg *ia = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ia->ifa_index)) == NULL){
		octx->diagnostic("Invalid interface index: %d\n",ia->ifa_index);
		return -1;
	}
	octx->diagnostic("[%8s] ADDRESS DELETED\n",iface->name);
	// FIXME
	return 0;
}

static int
handle_rtm_newaddr(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifaddrmsg *ia = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ia->ifa_index)) == NULL){
		octx->diagnostic("Invalid interface index: %d\n",ia->ifa_index);
		return -1;
	}
	octx->diagnostic("[%8s] ADDRESS ADDED\n",iface->name);
	// FIXME
	return 0;
}*/

static int
handle_rtm_dellink(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		octx->diagnostic("Invalid interface index: %d",ii->ifi_index);
		return -1;
	}
	free_iface(octx,iface); // calls octx->iface_removed
	return 0;
}

typedef struct psocket_marsh {
	const omphalos_iface *octx;
	interface *i;
	int cancelled;
	pthread_cond_t cond;
	pthread_mutex_t lock;
	pthread_t tid;
} psocket_marsh;

static int
ring_packet_loop(psocket_marsh *pm){
	// FIXME either send pm and nothing or everything else this aliases
	unsigned idx = 0;
	void *rxm;

	rxm = pm->i->rxm;
	while(!pm->cancelled){
		int r;

		if( (r = pthread_mutex_lock(&pm->i->lock)) ){
			pm->octx->diagnostic("Couldn't lock on %s (%s?)",
					pm->i->name,strerror(r));
			return -1;
		}
		if((r = handle_ring_packet(pm->octx,pm->i,pm->i->rfd,rxm)) == 0){
			rxm += inclen(&idx,&pm->i->rtpr);
		}else if(r < 0){
			pthread_mutex_unlock(&pm->i->lock);
			return -1;
		}
		if( (r = pthread_mutex_unlock(&pm->i->lock)) ){
			pm->octx->diagnostic("Couldn't lock on %s (%s?)",
					pm->i->name,strerror(r));
			return -1;
		}
	}
	return 0;
}

static void *
psocket_thread(void *unsafe){
	psocket_marsh *pm = unsafe;
	int r;

	// We control thread exit via the global cancelled value, set in the
	// signal handler. We don't want actual pthread cancellation, as it's
	// unsafe for the user callback's duration, and thus we'd need switch
	// between enabled and disabled status.
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
	r = ring_packet_loop(pm);
	// 'cancelled' has been set globally. We must ensure that our death
	// signal has been sent before safely exiting.
	pthread_mutex_lock(&pm->lock);
	while(!pm->cancelled){
		pthread_cond_wait(&pm->cond,&pm->lock);
	}
	pthread_mutex_unlock(&pm->lock);
	return r ? "calamitous error" : PTHREAD_CANCELED;
}

static void
pmarsh_destroy(const omphalos_iface *octx,psocket_marsh *pm){
	if( (errno = pthread_cond_destroy(&pm->cond)) ){
		octx->diagnostic("Error cleaning condvar: %s",strerror(errno));
	}
	if( (errno = pthread_mutex_destroy(&pm->lock)) ){
		octx->diagnostic("Error cleaning mutex: %s",strerror(errno));
	}
	free(pm);
}

#include <assert.h>
void reap_thread(const omphalos_iface *octx,interface *i){
	void *ret;

	// See psocket_thread(); we disable pthread cancellation. Send a
	// signal instead, engaging internal cancellation. A signal is the only
	// choice for waking up a thread in poll(), also. Now, it is unsafe to
	// send a nonexistant thread a signal, so we must ensure the thread
	// never exits until ater we've signalled it. So, packet sockets:
	//
	//  - spin on fd, poll()ing until there's a packet or signal
	//  - check the global cancellation flag, set in the signal handler
	//  - if it's high, block on the pmarsh->cancelled variable
	//  - meanwhile, we set ->cancelled only after sending the signal
	//  - we use a condvar signal (safe) to wake it up following
	//  - the thread wakes, dies, and is joined
	//  - we safely close the fd and free the pmarsh
	pthread_mutex_lock(&i->pmarsh->lock);
		if(!i->pmarsh->cancelled){
			if( (errno = pthread_kill(i->pmarsh->tid,SIGCHLD)) ){
				octx->diagnostic("Couldn't signal thread (%s?)",strerror(errno));
			} // FIXME check return codes here
			i->pmarsh->cancelled = 1;
		}
	pthread_cond_signal(&i->pmarsh->cond);
	pthread_mutex_unlock(&i->pmarsh->lock);
	if( (errno = pthread_join(i->pmarsh->tid,&ret)) ){
		octx->diagnostic("Couldn't join thread (%s?)",strerror(errno));
	/*}else if(ret != PTHREAD_CANCELED){
		octx->diagnostic("%s thread returned error on exit (%s)",
				i->name,(char *)ret);*/
	}
	pmarsh_destroy(octx,i->pmarsh);
	i->pmarsh = NULL;
}

static psocket_marsh *
pmarsh_create(void){
	psocket_marsh *ret;

	if( (ret = malloc(sizeof(*ret))) ){
		if(pthread_mutex_init(&ret->lock,NULL) == 0){
			if(pthread_cond_init(&ret->cond,NULL) == 0){
				ret->cancelled = 0;
				return ret;
			}
			pthread_mutex_destroy(&ret->lock);
		}
		free(ret);
	}
	return NULL;
}

static int
prepare_packet_sockets(const omphalos_iface *octx,interface *iface,int idx){
	if( (iface->pmarsh = pmarsh_create()) ){
		if((iface->fd = packet_socket(octx,ETH_P_ALL)) >= 0){
			if((iface->ts = mmap_tx_psocket(octx,iface->fd,idx,
					iface->mtu,&iface->txm,&iface->ttpr)) > 0){
				if((iface->rfd = packet_socket(octx,ETH_P_ALL)) >= 0){
					if((iface->rs = mmap_rx_psocket(octx,iface->rfd,idx,
							iface->mtu,&iface->rxm,&iface->rtpr)) > 0){
						iface->pmarsh->octx = octx;
						iface->pmarsh->i = iface;
						iface->curtxm = iface->txm;
						iface->txidx = 0;
						if(pthread_create(&iface->pmarsh->tid,NULL,
								psocket_thread,iface->pmarsh) == 0){
							return 0;
						}
					}
					close(iface->rfd); // munmaps
				}
			}
			close(iface->fd); // munmaps
		}
		pmarsh_destroy(octx,iface->pmarsh);
		iface->pmarsh = NULL;
	}
	// Everything needs already be closed/freed by the time we get here
	iface->rfd = iface->fd = -1;
	memset(&iface->rtpr,0,sizeof(iface->rtpr));
	memset(&iface->ttpr,0,sizeof(iface->ttpr));
	iface->rxm = iface->txm = NULL;
	iface->ts = iface->rs = 0;
	return -1;
}

static char *
name_virtual_device(const struct ifinfomsg *ii,struct ethtool_drvinfo *ed){
	if(ii->ifi_type == ARPHRD_LOOPBACK){
		return strdup("Linux IPv4/IPv6 loopback device");
	}else if(ii->ifi_type == ARPHRD_TUNNEL){
		return strdup("Linux IPIP unicast IP4-in-IP4 tunnel");
	}else if(ii->ifi_type == ARPHRD_TUNNEL6){
		return strdup("Linux IP6IP6 tunnel");
	}else if(ii->ifi_type == ARPHRD_SIT){
		return strdup("Linux Simple Internet Transition IP6-in-IP4 tunnel");
	}else if(ii->ifi_type == ARPHRD_IPGRE){
		return strdup("Linux Generic Routing Encapsulation IP-in-GRE tunnel");
	}else if(ii->ifi_type == ARPHRD_VOID){
		// These can be a number of things...
		//  teqlX - trivial traffic equalizer
		return strdup("Linux metadevice");
	}else if(ed){
		if(strcmp(ed->driver,"tun") == 0){
			if(strcmp(ed->bus_info,"tap") == 0){
				return strdup("Linux Ethernet TAP device");
			}else if(strcmp(ed->bus_info,"tun") == 0){
				return strdup("Linux IPv4 point-to-point TUN device");
			}
		}else if(strcmp(ed->driver,"bridge") == 0){
			return strdup("Linux Ethernet bridge");
		}else if(strcmp(ed->driver,"vif") == 0){
			return strdup("Xen virtual Ethernet interface");
		}
	}
	return NULL;
}

static int
handle_newlink_locked(const omphalos_iface *octx,interface *iface,
			const struct ifinfomsg *ii,const struct nlmsghdr *nl){
	const struct rtattr *ra;
	int rlen;

	// FIXME this is all crap
	if(iface->name == NULL){
		if(timestat_prep(&iface->fps,IFACE_TIMESTAT_USECS,IFACE_TIMESTAT_SLOTS)){
			return -1;
		}
		if(timestat_prep(&iface->bps,IFACE_TIMESTAT_USECS,IFACE_TIMESTAT_SLOTS)){
			timestat_destroy(&iface->fps);
			return -1;
		}
	}
	iface->arptype = ii->ifi_type;
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*ii));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*ii));
	// FIXME this is all no good. error paths allow partial updates of
	// the return interface and memory leaks...
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
			case IFLA_ADDRESS:{
				char *addr;

				if(iface->addrlen && iface->addrlen != RTA_PAYLOAD(ra)){
					octx->diagnostic("Address illegal: %lu",RTA_PAYLOAD(ra));
					return -1;
				}
				if((addr = malloc(RTA_PAYLOAD(ra))) == NULL){
					octx->diagnostic("Address too long: %lu",RTA_PAYLOAD(ra));
					return -1;
				}
				memcpy(addr,RTA_DATA(ra),RTA_PAYLOAD(ra));
				free(iface->addr);
				iface->addr = addr;
				iface->addrlen = RTA_PAYLOAD(ra);
			break;}case IFLA_BROADCAST:{
				char *addr;

				if(iface->addrlen && iface->addrlen != RTA_PAYLOAD(ra)){
					octx->diagnostic("Broadcast illegal: %lu",RTA_PAYLOAD(ra));
					return -1;
				}
				if((addr = malloc(RTA_PAYLOAD(ra))) == NULL){
					octx->diagnostic("Broadcast too long: %lu",RTA_PAYLOAD(ra));
					return -1;
				}
				memcpy(addr,RTA_DATA(ra),RTA_PAYLOAD(ra));
				free(iface->bcast);
				iface->bcast = addr;
				iface->addrlen = RTA_PAYLOAD(ra);
			break;}case IFLA_IFNAME:{
				char *name;

				if((name = strdup(RTA_DATA(ra))) == NULL){
					octx->diagnostic("Name too long: %lu",RTA_PAYLOAD(ra));
					return -1;
				}
				free(iface->name);
				iface->name = name;
			break;}case IFLA_MTU:{
				if(RTA_PAYLOAD(ra) != sizeof(int)){
					octx->diagnostic("Expected %zu MTU bytes, got %lu",
							sizeof(int),RTA_PAYLOAD(ra));
					break;
				}
				iface->mtu = *(int *)RTA_DATA(ra);
			break;}case IFLA_LINK:{
			break;}case IFLA_MASTER:{ // bridging event
				if(RTA_PAYLOAD(ra) == sizeof(int)){
					octx->diagnostic("Bridging event on %s (%d)",iface->name,RTA_DATA(ra));
				}
			break;}case IFLA_PROTINFO:{
				octx->diagnostic("Protocol info message on %s",iface->name);
			break;}case IFLA_TXQLEN:{
			break;}case IFLA_MAP:{
			break;}case IFLA_WEIGHT:{
			break;}case IFLA_QDISC:{
			break;}case IFLA_STATS:{
			break;}case IFLA_WIRELESS:{
				if(handle_wireless_event(octx,iface,RTA_DATA(ra),RTA_PAYLOAD(ra)) < 0){
					return -1;
				}
				return 0; // FIXME is this safe? see bug 277
			break;}case IFLA_OPERSTATE:{
			break;}case IFLA_LINKMODE:{
			break;}case IFLA_LINKINFO:{
			break;}case IFLA_NET_NS_PID:{
			break;}case IFLA_IFALIAS:{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
			break;}case IFLA_NUM_VF:{
			break;}case IFLA_STATS64:{
				// see http://git390.marist.edu/cgi-bin/gitweb.cgi?p=linux-2.6.git;a=commitdiff_plain;h=10708f37ae729baba9b67bd134c3720709d4ae62
				// added in 2.6.35-rc1
			break;}case IFLA_VF_PORTS:{
			break;}case IFLA_PORT_SELF:{
			break;}case IFLA_AF_SPEC:{
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)
			break;}case IFLA_GROUP:{
#endif
			break;}default:{
				octx->diagnostic("Unknown iflatype %u on %s",
						ra->rta_type,iface->name);
			break;}
		}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		octx->diagnostic("%d excess bytes on newlink message",rlen);
	}
	// FIXME memory leaks on failure paths, ahoy!
	if(iface->name == NULL){
		octx->diagnostic("No name in link message");
		return -1;
	}
	if(lookup_arptype(ii->ifi_type,&iface->analyzer) == NULL){
		octx->diagnostic("Unknown device type for %s",iface->name);
		return -1;
	}
	if(iface->mtu == 0){
		octx->diagnostic("No MTU in new link message for %s",iface->name);
		return -1;
	}
	iface->flags = ii->ifi_flags;
	iface->settings_valid = SETTINGS_INVALID;
	// Ethtool can fail for any given command depending on the device's
	// level of support. All but loopback seem to provide driver info...
	if(iface_driver_info(octx,iface->name,&iface->drv)){
		memset(&iface->drv,0,sizeof(iface->drv));
		iface->topinfo.devname = name_virtual_device(ii,NULL);
	}else{
		if((iface->busname = lookup_bus(iface->drv.bus_info,&iface->topinfo)) == NULL){
			iface->topinfo.devname = name_virtual_device(ii,&iface->drv);
		}else{
			// Try to get detailed wireless info first, falling back to ethtool.
			if(iface_wireless_info(octx,iface->name,&iface->settings.wext) == 0){
				iface->settings_valid = SETTINGS_VALID_WEXT;
			}else if(iface_ethtool_info(octx,iface->name,&iface->settings.ethtool) == 0){
				iface->settings_valid = SETTINGS_VALID_ETHTOOL;
			}
		}
	}
	// Offload info seems available for everything, even loopback.
	iface_offload_info(octx,iface->name,&iface->offload,&iface->offloadmask);

	// Bring down or set up the packet sockets and thread, as appropriate
	if(iface->fd < 0 && (iface->flags & IFF_UP)){
		iface->opaque = octx->iface_event(iface,iface->opaque);
		if(iface->addr){
			lookup_l2host(octx,iface,iface->addr);
		}
		if(iface->bcast && (iface->flags & IFF_BROADCAST)){
			lookup_l2host(octx,iface,iface->bcast);
		}
		prepare_packet_sockets(octx,iface,ii->ifi_index);
	}else{
		// Ensure there's L2 host entries for the device's address and any
		// appropriate link broadcast adddress.
		if(iface->pmarsh && !(iface->flags & IFF_UP)){
			// See note in free_iface() about operation ordering here.
			reap_thread(octx,iface);
			close(iface->rfd);
			close(iface->fd);
			memset(&iface->ttpr,0,sizeof(iface->ttpr));
			memset(&iface->rtpr,0,sizeof(iface->rtpr));
			iface->rfd = iface->fd = -1;
			iface->rs = iface->ts = 0;
		}
		iface->opaque = octx->iface_event(iface,iface->opaque);
	}
	return 0;
}

static int
handle_rtm_newlink(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	interface *iface;
	int r;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		octx->diagnostic("Invalid interface index: %d",ii->ifi_index);
		return -1;
	}
	if( (r = pthread_mutex_lock(&iface->lock)) ){
		octx->diagnostic("Couldn't get lock for %d (%s?)",ii->ifi_index,strerror(r));
		return -1;
	}
	r = handle_newlink_locked(octx,iface,ii,nl);
	pthread_mutex_unlock(&iface->lock);
	return r;
}

static int
handle_netlink_error(const omphalos_iface *octx,int fd,const struct nlmsgerr *nerr){
	if(nerr->error == 0){
		octx->diagnostic("ACK on netlink %d msgid %u type %u",
			fd,nerr->msg.nlmsg_seq,nerr->msg.nlmsg_type);
		// FIXME do we care?
		return 0;
	}
	if(-nerr->error == EAGAIN || -nerr->error == EBUSY){
		switch(nerr->msg.nlmsg_type){
		case RTM_GETLINK:{
			return discover_links(octx,fd);
		break;}case RTM_GETADDR:{
			return discover_addrs(octx,fd);
		break;}case RTM_GETROUTE:{
			return discover_routes(octx,fd);
		break;}case RTM_GETNEIGH:{
			return discover_neighbors(octx,fd);
		break;}default:{
			octx->diagnostic("Unknown msgtype in EAGAIN: %u",nerr->msg.nlmsg_type);
			return -1;
		break;}
		}
	}
	octx->diagnostic("Error message on netlink %d msgid %u type %u (%s?)",
		fd,nerr->msg.nlmsg_seq,nerr->msg.nlmsg_type,strerror(-nerr->error));
	return -1;
}

static int
handle_netlink_event(const omphalos_iface *octx,int fd){
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
			//octx->diagnostic("MSG TYPE %d\n",(int)nh->nlmsg_type);
			if(nh->nlmsg_flags & NLM_F_MULTI){
				inmulti = 1;
			}
			switch(nh->nlmsg_type){
			case RTM_NEWLINK:{
				res |= handle_rtm_newlink(octx,nh);
			break;}case RTM_DELLINK:{
				res |= handle_rtm_dellink(octx,nh);
			break;}case RTM_NEWNEIGH:{
				res |= handle_rtm_newneigh(octx,nh);
			break;}case RTM_DELNEIGH:{
				res |= handle_rtm_delneigh(octx,nh);
			break;}case RTM_NEWROUTE:{
				res |= handle_rtm_newroute(octx,nh);
			break;}case RTM_DELROUTE:{
				res |= handle_rtm_delroute(octx,nh);
			break;}case RTM_NEWADDR:{
			break;}case RTM_DELADDR:{
			break;}case NLMSG_DONE:{
				if(!inmulti){
					octx->diagnostic("Warning: DONE outside multipart on %d",fd);
				}
				inmulti = 0;
			break;}case NLMSG_ERROR:{
				res |= handle_netlink_error(octx,fd,NLMSG_DATA(nh));
			break;}default:{
				octx->diagnostic("Unknown netlink msgtype %u on %d",nh->nlmsg_type,fd);
				res = -1;
			break;}}
		}
	}
	if(inmulti){
		octx->diagnostic("Warning: unterminated multipart on %d",fd);
		res = -1;
	}
	if(r < 0 && errno != EAGAIN){
		octx->diagnostic("Error reading netlink socket %d (%s?)",
				fd,strerror(errno));
		res = -1;
	}else if(r == 0){
		octx->diagnostic("EOF on netlink socket %d",fd);
		// FIXME reopen...?
		res = -1;
	}
	return res;
}

static int
netlink_thread(const omphalos_iface *octx){
	struct pollfd pfd[2] = {
		{
			.events = POLLIN | POLLRDNORM | POLLERR,
		},
		{
			.events = POLLIN | POLLRDNORM | POLLERR,
		}
	};
	int(* const callbacks[2])(const omphalos_iface *,int) = {
		handle_netlink_event,
		handle_watch_event,
	};
	int events;

	if((pfd[1].fd = watch_init(octx)) < 0){
		return -1;
	}
	if((pfd[0].fd = netlink_socket(octx)) < 0){
		watch_stop(octx);
		return -1;
	}
	if(discover_bluetooth(octx)){
		close(pfd[0].fd);
		watch_stop(octx);
		return -1;
	}
	if(discover_links(octx,pfd[0].fd)){
		close(pfd[0].fd);
		watch_stop(octx);
		return -1;
	}
	if(discover_neighbors(octx,pfd[0].fd)){
		close(pfd[0].fd);
		watch_stop(octx);
		return -1;
	}
	if(discover_routes(octx,pfd[0].fd)){
		close(pfd[0].fd);
		watch_stop(octx);
		return -1;
	}
	while(!cancelled){
		unsigned z;

		errno = 0;
		while((events = poll(pfd,sizeof(pfd) / sizeof(*pfd),-1)) == 0){
			octx->diagnostic("Spontaneous wakeup on netlink socket %d",pfd[0].fd);
		}
		if(events < 0){
			if(errno != EINTR){
				octx->diagnostic("Error polling core sockets (%s?)",strerror(errno));
			}
			continue;
		}
		for(z = 0 ; z < sizeof(pfd) / sizeof(*pfd) ; ++z){
			if(pfd[z].revents & POLLERR){
				octx->diagnostic("Error polling socket %d\n",pfd[z].fd);
			}else if(pfd[z].revents){
				callbacks[z](octx,pfd[z].fd);
			}
			pfd[z].revents = 0;
		}
	}
	octx->diagnostic("Shutting down (cancelled = %u)...",cancelled);
	watch_stop(octx);
	close(pfd[0].fd);
	return 0;
}

static int
restore_sigmask(const omphalos_iface *octx){
	sigset_t csigs;

	if(sigemptyset(&csigs) || sigaddset(&csigs,SIGINT)){
		octx->diagnostic("Couldn't prepare sigset (%s?)",strerror(errno));
		return -1;
	}
	if(pthread_sigmask(SIG_BLOCK,&csigs,NULL)){
		octx->diagnostic("Couldn't mask signals (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

static int
restore_sighandler(const omphalos_iface *pctx){
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_RESTART,
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
		// SA_RESTART doesn't apply to all functions; most of the time,
		// we're sitting in poll(), which is *not* restarted...
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

int handle_netlink_socket(const omphalos_ctx *pctx){
	int ret;

	if(setup_sighandler(&pctx->iface)){
		return -1;
	}
	ret = netlink_thread(&pctx->iface);
	ret |= restore_sighandler(&pctx->iface);
	ret |= restore_sigmask(&pctx->iface);
	return ret;
}
