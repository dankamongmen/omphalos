#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <omphalos/diag.h>
#include <linux/version.h>
#include <omphalos/route.h>
#include <linux/rtnetlink.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME need locking on all of this!!!

// FIXME use a multibit trie rather than ultra-lame linked list yeargh
// see varghese's 'networking algorithmics' sec 11.8
typedef struct route {
	interface *iface;
	sa_family_t family;
	struct sockaddr_storage sss,ssd,ssg;
	unsigned maskbits;
	struct route *next;
} route;

static route *ip_table4,*ip_table6;
static pthread_mutex_t route_lock = PTHREAD_MUTEX_INITIALIZER;

// To do longest-match routing, we need to keep:
//  (a) a partition of the address space, pointing to the longest match for
//       each distinct section. each node stores 1 or more (in the case of
//       equal-length multipath) routes, and a pointer to:
//  (b) the next-longest match.
//
// A route may be pointed at by more than one node (the default routes will be
// eventually pointed to by any and all nodes save their own), but does not
// point to more than one node.
static route *
create_route(void){
	route *r;

	if( (r = malloc(sizeof(*r))) ){
		memset(r,0,sizeof(*r));
	}
	return r;
}

static void
free_route(route *r){
	free(r);
}

int handle_rtm_delroute(const struct nlmsghdr *nl){
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
			diagnostic(L"Unknown rtatype %u",ra->rta_type);
			break;
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		diagnostic(L"%d excess bytes on newlink message",rlen);
	}
	// FIXME handle it!
	return 0;
}

int handle_rtm_newroute(const struct nlmsghdr *nl){
	const struct rtmsg *rt = NLMSG_DATA(nl);
	struct rtattr *ra;
	void *as,*ad,*ag;
	int rlen,oif;
	route *r,**prev;
	size_t flen;

	oif = -1;
	if((r = create_route()) == NULL){
		return -1;
	}
	switch( (r->family = rt->rtm_family) ){
	case AF_INET:{
		flen = sizeof(uint32_t);
		as = &((struct sockaddr_in *)&r->sss)->sin_addr;
		ad = &((struct sockaddr_in *)&r->ssd)->sin_addr;
		ag = &((struct sockaddr_in *)&r->ssg)->sin_addr;
	break;}case AF_INET6:{
		flen = sizeof(uint32_t) * 4;
		as = &((struct sockaddr_in6 *)&r->sss)->sin6_addr;
		ad = &((struct sockaddr_in6 *)&r->ssd)->sin6_addr;
		ag = &((struct sockaddr_in6 *)&r->ssg)->sin6_addr;
	break;}case AF_BRIDGE:{
		// FIXME wtf is a bridge route
		diagnostic(L"got a bridge route hrmmm FIXME");
		return -1; // FIXME
	break;}default:{
		flen = 0;
	break;} }
	r->maskbits = rt->rtm_dst_len;
	if(flen == 0 || flen > sizeof(r->sss.__ss_padding)){
		diagnostic(L"Unknown route family %u",rt->rtm_family);
		return -1;
	}
	rlen = nl->nlmsg_len - NLMSG_LENGTH(sizeof(*rt));
	ra = (struct rtattr *)((char *)(NLMSG_DATA(nl)) + sizeof(*rt));
	memset(&r->ssg,0,sizeof(r->ssg));
	memset(&r->ssd,0,sizeof(r->ssd));
	memset(&r->sss,0,sizeof(r->sss));
	while(RTA_OK(ra,rlen)){
		switch(ra->rta_type){
		case RTA_DST:{
			if(RTA_PAYLOAD(ra) != flen){
				diagnostic(L"Expected %zu dst bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			if(r->ssd.ss_family){
				diagnostic(L"Got two destinations for route");
				break;
			}
			memcpy(ad,RTA_DATA(ra),flen);
			r->ssd.ss_family = r->family;
		break;}case RTA_PREFSRC: case RTA_SRC:{
			// FIXME do we not want to prefer PREFSRC?
			if(RTA_PAYLOAD(ra) != flen){
				diagnostic(L"Expected %zu src bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			if(r->sss.ss_family){
				diagnostic(L"Got two sources for route");
				break;
			}
			memcpy(as,RTA_DATA(ra),flen);
			r->sss.ss_family = r->family;
		break;}case RTA_IIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				diagnostic(L"Expected %zu iiface bytes, got %lu",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			// we don't use RTA_OIF: iif = *(int *)RTA_DATA(ra);
		break;}case RTA_OIF:{
			if(RTA_PAYLOAD(ra) != sizeof(int)){
				diagnostic(L"Expected %zu oiface bytes, got %lu",
						sizeof(int),RTA_PAYLOAD(ra));
				break;
			}
			oif = *(int *)RTA_DATA(ra);
		break;}case RTA_GATEWAY:{
			if(RTA_PAYLOAD(ra) != flen){
				diagnostic(L"Expected %zu gw bytes, got %lu",
						flen,RTA_PAYLOAD(ra));
				break;
			}
			if(r->ssg.ss_family){
				diagnostic(L"Got two gateways for route");
				break;
			}
			// We get 0.0.0.0 as the gateway when there's no 'via'
			if(memcmp(ag,RTA_DATA(ra),flen)){
				memcpy(ag,RTA_DATA(ra),flen);
				r->ssg.ss_family = r->family;
			}
		break;}case RTA_PRIORITY:{
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
			diagnostic(L"Unknown rtatype %u",ra->rta_type);
		break;}}
		ra = RTA_NEXT(ra,rlen);
	}
	if(rlen){
		diagnostic(L"%d excess bytes on newlink message",rlen);
	}
	if((r->iface = iface_by_idx(oif)) == NULL){
		diagnostic(L"Unknown output interface %d on %s",oif,r->iface->name);
		goto err;
	}
	{
		char str[INET6_ADDRSTRLEN],via[INET6_ADDRSTRLEN];
		inet_ntop(rt->rtm_family,ad,str,sizeof(str));
		inet_ntop(rt->rtm_family,ag,via,sizeof(via));
		diagnostic(L"[%s] new route to %s/%u %ls%ls%s",
			r->iface->name,str,r->maskbits,
			rt->rtm_type == RTN_LOCAL ? L"(local)" :
			rt->rtm_type == RTN_BROADCAST ? L"(broadcast)" :
			rt->rtm_type == RTN_UNREACHABLE ? L"(unreachable)" :
			rt->rtm_type == RTN_ANYCAST ? L"(anycast)" :
			rt->rtm_type == RTN_UNICAST ? L"(unicast)" :
			rt->rtm_type == RTN_MULTICAST ? L"(multicast)" :
			rt->rtm_type == RTN_BLACKHOLE ? L"(blackhole)" :
			rt->rtm_type == RTN_MULTICAST ? L"(multicast)" : L"",
			r->ssg.ss_family ? L" via " : L"",
			r->ssg.ss_family ? via : "");
	}
	// We're not interest in blackholes, unreachables, prohibits, NATs yet
	if(rt->rtm_type != RTN_UNICAST && rt->rtm_type != RTN_LOCAL
			&& rt->rtm_type != RTN_BROADCAST
			&& rt->rtm_type != RTN_ANYCAST
			&& rt->rtm_type != RTN_MULTICAST){
		free_route(r);
		return 0;
	}
	assert(r->iface);
	if(!r->sss.ss_family){
		struct routepath rp;

		if(get_router(r->sss.ss_family,ad,&rp) == 0){
			if(r->sss.ss_family == AF_INET){
				memcpy(as,&rp.src[0],4);
			}else if(r->sss.ss_family == AF_INET6){
				memcpy(as,&rp.src[0],16);
			}else{
				assert(0);
			}
		}else{ // FIXME vicious hackery!
			if(r->family == AF_INET6){
				memcpy(as,&r->iface->ip6defsrc,flen);
				r->sss.ss_family = AF_INET6;
			}
		}
	}
	if(r->family == AF_INET){
		lock_interface(r->iface);
		if(add_route4(r->iface,ad,r->ssg.ss_family ? ag : NULL,
					r->sss.ss_family ? as : NULL,
					r->maskbits)){
			unlock_interface(r->iface);
			diagnostic(L"Couldn't add route to %s",r->iface->name);
			goto err;
		}
		if(r->ssg.ss_family){
			send_arp_probe(r->iface,r->iface->bcast,ag,as);
		}
		unlock_interface(r->iface);
		pthread_mutex_lock(&route_lock);
			prev = &ip_table4;
			// Order most-specific (largest maskbits) to least-specific (0 maskbits)
			while(*prev){
				if(r->maskbits > (*prev)->maskbits){
					break;
				}
				prev = &(*prev)->next;
			}
			r->next = *prev;
			*prev = r;
			if(r->sss.ss_family){
				while( *(prev = &(*prev)->next) ){
					assert((*prev)->maskbits < r->maskbits);
					if(!((*prev)->sss.ss_family)){
						memcpy(&(*prev)->sss,&r->sss,sizeof(r->sss));
					}
				}
			}
		pthread_mutex_unlock(&route_lock);
	}else if(r->family == AF_INET6){
		lock_interface(r->iface);
		if(add_route6(r->iface,ad,r->ssg.ss_family ? ag : NULL,r->sss.ss_family ? as : NULL,r->maskbits)){
			unlock_interface(r->iface);
			diagnostic(L"Couldn't add route to %s",r->iface->name);
			goto err;
		}
		unlock_interface(r->iface);
		pthread_mutex_lock(&route_lock);
			prev = &ip_table6;
			// Order most-specific (largest maskbits) to least-specific (0 maskbits)
			while(*prev){
				if(r->maskbits > (*prev)->maskbits){
					break;
				}
				prev = &(*prev)->next;
			}
			r->next = *prev;
			*prev = r;
			// FIXME set less-specific sources
		pthread_mutex_unlock(&route_lock);
	}
	return 0;

err:
	free_route(r);
	return -1;
}

int is_router(int fam,const void *addr){
	size_t gwoffset,len;
	route *rt;

	// FIXME we will want an actual cross-interface routing table rather
	// than iterating over all interfaces, eek
	if(fam == AF_INET){
		rt = ip_table4;
		len = 4;
		gwoffset = offsetof(struct sockaddr_in,sin_addr);
	}else if(fam == AF_INET6){
		rt = ip_table6;
		len = 16;
		gwoffset = offsetof(struct sockaddr_in6,sin6_addr);
	}else{
		return -1;
	}
	pthread_mutex_lock(&route_lock);
	while(rt){
		if(memcmp((const char *)(&rt->ssg) + gwoffset,addr,len) == 0){
			break;
		}
		rt = rt->next;
	}
	pthread_mutex_unlock(&route_lock);
	return rt ? 1 : 0;
}

// Determine how to send a packet to a layer 3 address.
// FIXME this whole function is just incredibly godawful
int get_router(int fam,const void *addr,struct routepath *rp){
	uint128_t maskaddr,gw;
	size_t gwoffset,len;
	route *rt;

	// FIXME we will want an actual cross-interface routing table rather
	// than iterating over all interfaces, eek
	if(fam == AF_INET){
		rt = ip_table4;
		len = 4;
		maskaddr[0] = *(const uint32_t *)addr;
		gwoffset = offsetof(struct sockaddr_in,sin_addr);
	}else if(fam == AF_INET6){
		rt = ip_table6;
		len = 16;
		memcpy(&maskaddr,&addr,len);
		gwoffset = offsetof(struct sockaddr_in6,sin6_addr);
	}else{
		return -1;
	}
	pthread_mutex_lock(&route_lock);
	while(rt){
		uint128_t tmp = { 0, 0, 0, 0 }; // FIXME so lame
		unsigned z;

		// Construct a properly-masked version of maskaddr
		for(z = 0 ; z < len / 4 ; ++z){
			if(rt->maskbits > 32 * z){
				if(rt->maskbits >= 32 * (z + 1)){
					tmp[z] = maskaddr[z];
				}else{
					uint32_t mask = ~0U << (32 - (rt->maskbits % 32));

					tmp[z] = htonl(ntohl(maskaddr[z]) & mask);
				}
			}else{
				tmp[z] = 0;
			}
		}
		if(memcmp((const char *)(&rt->ssd) + gwoffset,&tmp[0],len) == 0){
			break;
		}
		rt = rt->next;
	}
	if(rt){
		rp->i = rt->iface;
		memcpy(&rp->src,(const char *)(&rt->sss) + gwoffset,len);
		memset(&gw,0,sizeof(gw));
		memcpy(&gw,rt->ssg.ss_family ? (const char *)&rt->ssg + gwoffset : addr,len);
		if( (rp->l3 = find_l3host(rp->i,fam,&gw)) ){
			if( (rp->l2 = l3_getlastl2(rp->l3)) ){
				pthread_mutex_unlock(&route_lock);
				return 0;
			}
		}
	}
	pthread_mutex_unlock(&route_lock);
	return -1;
}

// Call get_router() on the address, acquire a TX frame from the discovered
// interface, Initialize it with proper L2 and L3 data.
int get_routed_frame(int fam,const void *addr,struct routepath *rp,
			void **frame,size_t *flen,size_t *hlen){
	if(get_router(fam,addr,rp)){
		return -1;
	}
	if((*frame = get_tx_frame(rp->i,flen)) == NULL){
		return -1;
	}
	assert(hlen); // FIXME set up the l2/l3 headers
	return -1;
}

void free_routes(void){
	route *rt;

	pthread_mutex_lock(&route_lock);
	while( (rt = ip_table4) ){
		ip_table4 = rt->next;
		free_route(rt);
	}
	while( (rt = ip_table6) ){
		ip_table6 = rt->next;
		free_route(rt);
	}
	pthread_mutex_unlock(&route_lock);
	/// pthread_mutex_destroy(&route_lock);
}
