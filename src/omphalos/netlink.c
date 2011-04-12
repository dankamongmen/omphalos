#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

int netlink_socket(void){
	struct sockaddr_nl sa;
	int fd;

	if((fd = socket(AF_NETLINK,SOCK_RAW,NETLINK_ROUTE)) < 0){
		fprintf(stderr,"Couldn't open NETLINK_ROUTE socket (%s?)\n",strerror(errno));
		return -1;
	}
	memset(&sa,0,sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTNLGRP_MAX;
	if(bind(fd,(const struct sockaddr *)&sa,sizeof(sa))){
		fprintf(stderr,"Couldn't bind NETLINK_ROUTE socket %d (%s?)\n",fd,strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

#define nldiscover(msg,famtype,famfield) do {\
	struct { struct nlmsghdr nh ; struct famtype m ; } req = { \
		.nh = { .nlmsg_len = NLMSG_LENGTH(sizeof(req.m)), \
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP, \
			.nlmsg_type = msg, }, \
		.m = { .famfield = AF_UNSPEC, }, }; \
	int r; \
	if((r = send(fd,&req,req.nh.nlmsg_len,0)) < 0){ \
		fprintf(stderr,"Failure writing " #msg " to %d (%s?)\n",\
				fd,strerror(errno)); \
	} \
	return r; \
}while(0)

int discover_addrs(int fd){
	nldiscover(RTM_GETADDR,ifaddrmsg,ifa_family);
}

int discover_links(int fd){
	nldiscover(RTM_GETLINK,ifinfomsg,ifi_family);
}

int discover_routes(int fd){
	nldiscover(RTM_GETROUTE,rtmsg,rtm_family);
}

/* This is all pretty omphalos-specific from here on out */
#include <stdlib.h>
#include <sys/uio.h>
#include <net/ethernet.h>
#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <omphalos/interface.h>

typedef struct arptype {
	unsigned ifi_type;
	const char *name;
} arptype;

static arptype arptypes[] = {
	{
		.ifi_type = ARPHRD_LOOPBACK,
		.name = "Loopback",
	},{
		.ifi_type = ARPHRD_ETHER,
		.name = "Ethernet",
	},{
		.ifi_type = ARPHRD_IEEE80211,
		.name = "Wireless",
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
	printf("[%8s] NEIGHBOR ADDED\n",iface->name);
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
	printf("[%8s] NEIGHBOR DELETED\n",iface->name);
	// FIXME
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
	struct sockaddr_storage sss,ssd;
	unsigned maskbits;
} route;

static int
handle_rtm_newroute(const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	const interface *iface;
	struct rtattr *ra;
	int rlen,iif,oif;
	void *as,*ad;
	size_t flen;
	route r;

	memset(&r,0,sizeof(r));
	switch( (r.family = rt->rtm_family) ){
	case AF_INET:{
		flen = sizeof(uint32_t);
		as = &((struct sockaddr_in *)&r.sss)->sin_addr;
		ad = &((struct sockaddr_in *)&r.ssd)->sin_addr;
	break;}case AF_INET6:{
		flen = sizeof(uint32_t) * 4;
		as = &((struct sockaddr_in6 *)&r.sss)->sin6_addr;
		ad = &((struct sockaddr_in6 *)&r.ssd)->sin6_addr;
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
		break;}case RTA_MARK:{
		break;}default:{
			fprintf(stderr,"Unknown rtatype %u\n",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		fprintf(stderr,"%d excess bytes on newlink message\n",rlen);
	}
	if(oif){
		if((iface = iface_by_idx(oif)) == NULL){
			goto err;
		}
	}else{
		fprintf(stderr,"No output interface for route\n");
		goto err;
	}
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
	return 0;

err:
	return -1;
}

static int
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
					break;
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
		printf("[%8s][%s] %s %d %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
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

int handle_netlink_event(int fd){
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
			break;}case RTM_DELLINK:{
				res |= handle_rtm_dellink(nh);
			break;}case RTM_NEWNEIGH:{
				res |= handle_rtm_newneigh(nh);
			break;}case RTM_DELNEIGH:{
				res |= handle_rtm_delneigh(nh);
			break;}case RTM_NEWROUTE:{
				res |= handle_rtm_newroute(nh);
			break;}case RTM_DELROUTE:{
				res |= handle_rtm_delroute(nh);
			break;}case RTM_NEWADDR:{
				res |= handle_rtm_newaddr(nh);
			break;}case RTM_DELADDR:{
				res |= handle_rtm_deladdr(nh);
			break;}case NLMSG_DONE:{
				if(!inmulti){
					fprintf(stderr,"Warning: DONE outside multipart on %d\n",fd);
				}
				inmulti = 0;
			break;}case NLMSG_ERROR:{
				struct nlmsgerr *nerr = NLMSG_DATA(nh);

				if(nerr->error == 0){
					printf("ACK on netlink %d msgid %u type %u\n",
						fd,nerr->msg.nlmsg_seq,nerr->msg.nlmsg_type);
				}else{
					fprintf(stderr,"Error message on netlink %d msgid %u (%s?)\n",
						fd,nerr->msg.nlmsg_seq,strerror(-nerr->error));
					res = -1;
				}
			break;}default:{
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
