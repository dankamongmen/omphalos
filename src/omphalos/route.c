#include <sys/socket.h>
#include <linux/version.h>
#include <omphalos/route.h>
#include <linux/rtnetlink.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct route {
	sa_family_t family;
	struct sockaddr_storage sss,ssd,ssg;
	unsigned maskbits;
} route;

int handle_rtm_delroute(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	struct rtattr *ra;
	int rlen;

	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*rt));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case RTA_DST:{
		break;}case RTA_SRC:{
		break;}case RTA_IIF:{
		break;}case RTA_OIF:{
		break;}default:{
			octx->diagnostic("Unknown rtatype %u",ra->rta_type);
			break;
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		octx->diagnostic("%d excess bytes on newlink message",rlen);
	}
	return 0;
}

int handle_rtm_newroute(const omphalos_iface *octx,const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	void *as,*ad,*ag,*pag,*pas;
	struct rtattr *ra;
	int rlen,iif,oif;
	interface *iface;
	size_t flen;
	route r;

	iif = oif = -1;
	pas = pag = NULL; // pointers set only once as/ag are copied into
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
		octx->diagnostic("Unknown route family %u",rt->rtm_family);
		return -1;
	}
	memset(ag,0,flen);
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*rt));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case RTA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu src bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
		break;}case RTA_SRC:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu src bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(as,RTA_DATA(ra),flen);
			pas = as;
		break;}case RTA_IIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				octx->diagnostic("Expected %zu iface bytes, got %lu",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			iif = *(int *)RTA_DATA(ra);
		break;}case RTA_OIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				octx->diagnostic("Expected %zu iface bytes, got %lu",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			oif = *(int *)RTA_DATA(ra);
		break;}case RTA_GATEWAY:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu gw bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			// We get 0.0.0.0 as the gateway when there's no 'via'
			if(memcmp(ag,RTA_DATA(ra),flen)){
				memcpy(ag,RTA_DATA(ra),flen);
				pag = ag;
			}
		break;}case RTA_PRIORITY:{
		break;}case RTA_PREFSRC:{
			if(RTA_PAYLOAD(ra) != flen){
				octx->diagnostic("Expected %zu src bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			memcpy(as,RTA_DATA(ra),flen);
			pas = as;
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
			octx->diagnostic("Unknown rtatype %u",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		octx->diagnostic("%d excess bytes on newlink message",rlen);
	}
	if(oif > -1){
		if((iface = iface_by_idx(oif)) == NULL){
			octx->diagnostic("Unknown output interface %d on %s",oif,iface->name);
			goto err;
		}
	}else{
		// blackhole routes typically have no output interface
		return 0;
	}
	if(r.family == AF_INET){
		if(add_route4(iface,ad,pag,pas,r.maskbits,iif)){
			octx->diagnostic("Couldn't add route to %s",iface->name);
			return -1;
		}
	}else if(r.family == AF_INET6){
		if(add_route6(iface,ad,pag,pas,r.maskbits,iif)){
			octx->diagnostic("Couldn't add route to %s",iface->name);
			return -1;
		}
	}
	// FIXME need a route callback in octx
	/*{
		char str[INET6_ADDRSTRLEN];
		inet_ntop(rt->rtm_family,ad,str,sizeof(str));
		octx->diagnostic("[%8s] route to %s/%u %s",iface->name,str,r.maskbits,
			rt->rtm_type == RTN_LOCAL ? "(local)" :
			rt->rtm_type == RTN_BROADCAST ? "(broadcast)" :
			rt->rtm_type == RTN_UNREACHABLE ? "(unreachable)" :
			rt->rtm_type == RTN_ANYCAST ? "(anycast)" :
			rt->rtm_type == RTN_UNICAST ? "(unicast)" :
			rt->rtm_type == RTN_MULTICAST ? "(multicast)" :
			rt->rtm_type == RTN_BLACKHOLE ? "(blackhole)" :
			rt->rtm_type == RTN_MULTICAST ? "(multicast)" :
			"");
	}*/
	return 0;

err:
	return -1;
}

