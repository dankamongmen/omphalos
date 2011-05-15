#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/version.h>
#include <linux/rtnetlink.h>
#include <omphalos/omphalos.h>

int netlink_socket(void){
	struct sockaddr_nl sa;
	int fd;

	if((fd = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE)) < 0){
		fprintf(stderr,"Couldn't open NETLINK_ROUTE socket (%s?)\n",strerror(errno));
		return -1;
	}
	memset(&sa,0,sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTNLGRP_NOTIFY | RTNLGRP_LINK | RTNLGRP_NEIGH |
			RTNLGRP_IPV4_ROUTE | RTNLGRP_IPV6_ROUTE;
	if(bind(fd,(const struct sockaddr *)&sa,sizeof(sa))){
		fprintf(stderr,"Couldn't bind NETLINK_ROUTE socket %d (%s?)\n",fd,strerror(errno));
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
	} \
	return r; \
}while(0)

int discover_addrs(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETADDR,ifaddrmsg,ifa_family);
}

int discover_links(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETLINK,ifinfomsg,ifi_family);
}

int discover_neighbors(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETNEIGH,ndmsg,ndm_family);
}

int discover_routes(const omphalos_iface *octx,int fd){
	nldiscover(octx,RTM_GETROUTE,rtmsg,rtm_family);
}

/* This is all pretty omphalos-specific from here on out */
#include <stdlib.h>
#include <sys/uio.h>
#include <net/ethernet.h>
#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <omphalos/ethtool.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/psocket.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>

static int
handle_rtm_newneigh(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	char ll[IFHWADDRLEN]; // FIXME get from selected interface
	struct sockaddr_storage ssd;
	struct rtattr *ra;
	struct l2host *l2;
	interface *iface;
	int rlen,llen;
	size_t flen;
	void *ad;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",nd->ndm_ifindex);
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
		fprintf(stderr,"Unknown route family %u\n",nd->ndm_family);
		return -1;
	}
	llen = 0;
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*nd));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*nd));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case NDA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				fprintf(stderr,"Expected %zu nw bytes, got %lu\n",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}case NDA_LLADDR:{
			llen = RTA_PAYLOAD(ra);
			if(llen){
				if(llen != sizeof(ll)){
					fprintf(stderr,"Expected %zu ll bytes, got %d\n",
						sizeof(ll),llen);
					llen = 0;
					break;
				}
				memcpy(ll,RTA_DATA(ra),sizeof(ll));
			}
		break;}case NDA_CACHEINFO:{
		break;}case NDA_PROBES:{
		break;}default:{
			fprintf(stderr,"Unknown ndatype %u\n",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	if(llen){
		l2 = lookup_l2host(ll,sizeof(ll));
		if(octx->neigh_event){
			iface->opaque = octx->neigh_event(iface,l2,iface->opaque);
		}
		// FIXME and do what else with it?
	}
	return 0;
}

static int
handle_rtm_delneigh(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ndmsg *nd = NLMSG_DATA(nl);
	char ll[IFHWADDRLEN]; // FIXME get from selected interface
	struct sockaddr_storage ssd;
	struct rtattr *ra;
	struct l2host *l2;
	interface *iface;
	int rlen,llen;
	size_t flen;
	void *ad;

	if((iface = iface_by_idx(nd->ndm_ifindex)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",nd->ndm_ifindex);
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
		fprintf(stderr,"Unknown route family %u\n",nd->ndm_family);
		return -1;
	}
	llen = 0;
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*nd));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*nd));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case NDA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				fprintf(stderr,"Expected %zu nw bytes, got %lu\n",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}case NDA_LLADDR:{
			llen = RTA_PAYLOAD(ra);
			if(llen){
				if(llen != sizeof(ll)){
					fprintf(stderr,"Expected %zu ll bytes, got %d\n",
						sizeof(ll),llen);
					llen = 0;
					break;
				}
				memcpy(ll,RTA_DATA(ra),sizeof(ll));
			}
		break;}case NDA_CACHEINFO:{
		break;}case NDA_PROBES:{
		break;}default:{
			fprintf(stderr,"Unknown ndatype %u\n",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	if(llen){
		l2 = lookup_l2host(ll,sizeof(ll));
		if(octx->neigh_removed){
			octx->neigh_removed(iface,l2,iface->opaque);
		}
		// FIXME and do what else with it?
	}
	return 0;
}

static int
handle_rtm_delroute(const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	struct rtattr *ra;
	int rlen;

	printf("ROUTE DELETED\n");
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*rt));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case RTA_DST:{
		break;}case RTA_SRC:{
		break;}case RTA_IIF:{
		break;}case RTA_OIF:{
		break;}default:{
			fprintf(stderr,"Unknown rtatype %u\n",ra->rta_type);
			break;
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	return 0;
}

typedef struct route {
	sa_family_t family;
	struct sockaddr_storage sss,ssd,ssg;
	unsigned maskbits;
} route;

static int
handle_rtm_newroute(const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	struct rtattr *ra;
	int rlen,iif,oif;
	void *as,*ad,*ag;
	interface *iface;
	size_t flen;
	route r;

	iif = oif = -1;
	memset(&r,0,sizeof(r));
	switch( (r.family = rt->rtm_family) ){
	case AF_INET:{
		flen = sizeof(uint32_t);
		as = &((struct sockaddr_in *)&r.sss)->sin_addr;
		ad = &((struct sockaddr_in *)&r.ssd)->sin_addr;
		ag = &((struct sockaddr_in *)&r.ssg)->sin_addr;
	break;}case AF_INET6:{
		flen = sizeof(uint32_t) * 4;
		as = &((struct sockaddr_in6 *)&r.sss)->sin6_addr;
		ad = &((struct sockaddr_in6 *)&r.ssd)->sin6_addr;
		ag = &((struct sockaddr_in6 *)&r.ssg)->sin6_addr;
	break;}default:{
		flen = 0;
	break;} }
	r.maskbits = rt->rtm_dst_len;
	if(flen == 0 || flen > sizeof(r.sss.__ss_padding)){
		fprintf(stderr,"Unknown route family %u\n",rt->rtm_family);
		return -1;
	}
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*rt));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case RTA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				fprintf(stderr,"Expected %zu src bytes, got %lu\n",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}case RTA_SRC:{
			if(RTA_PAYLOAD(ra) != flen){
				fprintf(stderr,"Expected %zu src bytes, got %lu\n",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(as,RTA_DATA(ra),flen);
		break;}case RTA_IIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				fprintf(stderr,"Expected %zu iface bytes, got %lu\n",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			iif = *(int *)RTA_DATA(ra);
		break;}case RTA_OIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				fprintf(stderr,"Expected %zu iface bytes, got %lu\n",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			oif = *(int *)RTA_DATA(ra);
		break;}case RTA_GATEWAY:{
			if(RTA_PAYLOAD(ra) != flen){
				fprintf(stderr,"Expected %zu gw bytes, got %lu\n",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ag,RTA_DATA(ra),flen);
		break;}case RTA_PRIORITY:{
		break;}case RTA_PREFSRC:{
		break;}case RTA_METRICS:{
		break;}case RTA_MULTIPATH:{
		// break;}case RTA_PROTOINFO:{ // unused
		break;}case RTA_FLOW:{
		break;}case RTA_CACHEINFO:{
		// break;}case RTA_SESSION:{ // unused
		// break;}case RTA_MP_ALGO:{ // unused
		break;}case RTA_TABLE:{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
		break;}case RTA_MARK:{
#endif
		break;}default:{
			fprintf(stderr,"Unknown rtatype %u\n",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	if(oif > -1){
		if((iface = iface_by_idx(oif)) == NULL){
			goto err;
		}
	}else{
		fprintf(stderr,"No output interface for route\n");
		goto err;
	}
	if(r.family == AF_INET){
		if(add_route4(iface,ad,ag,r.maskbits,iif)){
			return -1;
		}
	}else if(r.family == AF_INET6){
		if(add_route6(iface,ad,ag,r.maskbits,iif)){
			return -1;
		}
	}
	/*
	{
		char str[INET6_ADDRSTRLEN];
		inet_ntop(rt->rtm_family,ad,str,sizeof(str));
	printf("[%8s] route to %s/%u %s\n",iface->name,str,r.maskbits,
			rt->rtm_type == RTN_LOCAL ? "(local)" :
			rt->rtm_type == RTN_BROADCAST ? "(broadcast)" :
			rt->rtm_type == RTN_UNREACHABLE ? "(unreachable)" :
			rt->rtm_type == RTN_ANYCAST ? "(anycast)" :
			rt->rtm_type == RTN_UNICAST ? "(unicast)" :
			rt->rtm_type == RTN_MULTICAST ? "(multicast)" :
			rt->rtm_type == RTN_BLACKHOLE ? "(blackhole)" :
			rt->rtm_type == RTN_MULTICAST ? "(multicast)" :
			"");
	}
	*/
	return 0;

err:
	return -1;
}

/*static int
handle_rtm_deladdr(const struct nlmsghdr *nl){
	const struct ifaddrmsg *ia = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ia->ifa_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ia->ifa_index);
		return -1;
	}
	printf("[%8s] ADDRESS DELETED\n",iface->name);
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
	printf("[%8s] ADDRESS ADDED\n",iface->name);
	// FIXME
	return 0;
}*/

static int
handle_rtm_dellink(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	interface *iface;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ii->ifi_index);
		return -1;
	}
	if(octx->iface_removed){
		octx->iface_removed(iface,iface->opaque);
	}
	free_iface(iface);
	return 0;
}

typedef struct psocket_marsh {
	const omphalos_iface *octx;
	const interface *i;
} psocket_marsh;

static void *
psocket_thread(void *unsafe){
	const psocket_marsh *pm = unsafe;

	ring_packet_loop(pm->octx,pm->i->rfd,pm->i->rxm,&pm->i->rtpr);
	free(unsafe);
	return NULL;
}

static int
handle_rtm_newlink(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct ifinfomsg *ii = NLMSG_DATA(nl);
	const struct rtattr *ra;
	interface *iface;
	int rlen;

	if((iface = iface_by_idx(ii->ifi_index)) == NULL){
		fprintf(stderr,"Invalid interface index: %d\n",ii->ifi_index);
		return -1;
	}
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*ii));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*ii));
	// FIXME this is all no good. error paths allow partial updates of
	// the return interface and memory leaks...
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
			break;}case IFLA_BROADCAST:{
			break;}case IFLA_IFNAME:{
				char *name;

				if((name = strdup(RTA_DATA(ra))) == NULL){
					fprintf(stderr,"Name too long: %s\n",(char *)RTA_DATA(ra));
					return -1;
				}
				free(iface->name);
				iface->name = name;
			break;}case IFLA_MTU:{
				if(RTA_PAYLOAD(ra) != sizeof(int)){
					fprintf(stderr,"Expected %zu MTU bytes, got %lu\n",
							sizeof(int),RTA_PAYLOAD(ra));
					break;
				}
				iface->mtu = *(int *)RTA_DATA(ra);
			break;}case IFLA_LINK:{
			break;}case IFLA_TXQLEN:{
			break;}case IFLA_MAP:{
			break;}case IFLA_WEIGHT:{
			break;}case IFLA_QDISC:{
			break;}case IFLA_STATS:{
			break;}case IFLA_WIRELESS:{
				if(handle_wireless_event(octx,iface,RTA_DATA(ra),RTA_PAYLOAD(ra)) < 0){
					return -1;
				}
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
			break;}default:{
				fprintf(stderr,"Unknown iflatype %u\n",ra->rta_type);
			break;}
		}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	// FIXME memory leaks on failure paths, ahoy!
	if(iface->name == NULL){
		fprintf(stderr,"No name in new link message\n");
		return -1;
	}
	if(iface->mtu == 0){
		fprintf(stderr,"No MTU in new link message\n");
		return -1;
	}
	iface->arptype = ii->ifi_type;
	if(!(ii->ifi_flags & IFF_LOOPBACK)){
		if(iface_driver_info(iface->name,&iface->drv)){
			return -1;
		}
	}else{
		memset(&iface->drv,0,sizeof(iface->drv));
	}
	iface->flags = ii->ifi_flags;
	if(iface->fd < 0 && (iface->flags & IFF_UP)){
		psocket_marsh *pm;

		if((pm = malloc(sizeof(*pm))) == NULL){
			return -1;
		}
		if((iface->fd = packet_socket(octx,ETH_P_ALL)) < 0){
			free(pm);
			return -1;
		}
		if((iface->rfd = packet_socket(octx,ETH_P_ALL)) < 0){
			close(iface->fd);
			iface->fd = -1;
			free(pm);
			return -1;
		}
		if((iface->rs = mmap_rx_psocket(octx,iface->rfd,ii->ifi_index,
					iface->mtu,&iface->rxm,&iface->rtpr)) == 0){
			memset(&iface->rtpr,0,sizeof(iface->rtpr));
			iface->rxm = NULL;
			close(iface->rfd);
			close(iface->fd);
			iface->rfd = iface->fd = -1;
			free(pm);
			return -1;
		}
		if((iface->ts = mmap_tx_psocket(octx,iface->fd,ii->ifi_index,
					iface->mtu,&iface->txm,&iface->ttpr)) == 0){
			memset(&iface->rtpr,0,sizeof(iface->rtpr));
			memset(&iface->ttpr,0,sizeof(iface->ttpr));
			iface->rxm = iface->txm = NULL;
			close(iface->rfd);
			close(iface->fd);
			iface->rfd = iface->fd = -1;
			iface->rs = 0;
			free(pm);
			return -1;
		}
		pm->octx = octx;
		pm->i = iface;
		if(pthread_create(&iface->tid,NULL,psocket_thread,pm)){
			memset(&iface->rtpr,0,sizeof(iface->rtpr));
			memset(&iface->ttpr,0,sizeof(iface->ttpr));
			iface->rxm = iface->txm = NULL;
			close(iface->rfd);
			close(iface->fd);
			iface->rfd = iface->fd = -1;
			iface->rs = 0;
			free(pm);
			return -1;
		}
		/* FIXME
	if(unmap_psocket(rxm,rs)){
		ret = -1;
	}
	if(close(rfd)){
		pctx->iface.diagnostic("Couldn't close packet socket %d (%s?)",rfd,strerror(errno));
		ret = -1;
	}
	// ret |= reap_thread(nltid);
	*/
	}
	if(octx->iface_event){
		iface->opaque = octx->iface_event(iface,ii->ifi_index,iface->opaque);
	}
	return 0;
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

int handle_netlink_event(const omphalos_iface *octx,int fd){
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
				res |= handle_rtm_newlink(octx,nh);
			break;}case RTM_DELLINK:{
				res |= handle_rtm_dellink(octx,nh);
			break;}case RTM_NEWNEIGH:{
				res |= handle_rtm_newneigh(octx,nh);
			break;}case RTM_DELNEIGH:{
				res |= handle_rtm_delneigh(octx,nh);
			break;}case RTM_NEWROUTE:{
				res |= handle_rtm_newroute(nh);
			break;}case RTM_DELROUTE:{
				res |= handle_rtm_delroute(nh);
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
