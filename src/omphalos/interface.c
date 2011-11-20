#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/128.h>
#include <omphalos/util.h>
#include <omphalos/irda.h>
#include <omphalos/ietf.h>
#include <omphalos/service.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netlink.h>
#include <omphalos/psocket.h>
#include <omphalos/firewire.h>
#include <omphalos/omphalos.h>
#include <omphalos/netaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/interface.h>

#define MAXINTERFACES (1u << 16) // lame FIXME

static interface interfaces[MAXINTERFACES];

// FIXME what the hell to do here...?
static void
handle_void_packet(omphalos_packet *op,const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	const struct omphalos_ctx *ctx = get_octx();
	const omphalos_iface *octx = &ctx->iface;

	assert(op->i);
	if(octx->packet_read){
		octx->packet_read(op);
	}
}

int init_interfaces(void){
	pthread_mutexattr_t attr;
	unsigned i;

	if(pthread_mutexattr_init(&attr)){
		return -1;
	}
	if(pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE_NP)){
		pthread_mutexattr_destroy(&attr);
		return -1;
	}
	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		interface *iface = &interfaces[i];

		if(pthread_mutex_init(&iface->lock,&attr)){
			while(i--){
				pthread_mutex_destroy(&interfaces[i].lock);
			}
			pthread_mutexattr_destroy(&attr);
			return -1;
		}
		iface->fd4 = iface->fd6 = iface->rfd = iface->fd = -1;
		if(timestat_prep(&iface->fps,IFACE_TIMESTAT_USECS,IFACE_TIMESTAT_SLOTS)){
			return -1;
		}
		if(timestat_prep(&iface->bps,IFACE_TIMESTAT_USECS,IFACE_TIMESTAT_SLOTS)){
			timestat_destroy(&iface->fps);
			return -1;
		}
	}
	if(pthread_mutexattr_destroy(&attr)){
		return -1;
	}
	return 0;
}

#define STAT(fp,i,x) if((i)->x) { if(fwprintf((fp),L"<"#x">%ju</"#x">",(i)->x) < 0){ return -1; } }
int print_iface_stats(FILE *fp,const interface *i,interface *agg,const char *decorator){
	if(i->name == NULL){
		if(fwprintf(fp,L"<%s>",decorator) < 0){
			return -1;
		}
	}else{
		if(fwprintf(fp,L"<%s name=\"%s\">",decorator,i->name) < 0){
			return -1;
		}
	}
	STAT(fp,i,frames);
	STAT(fp,i,truncated);
	STAT(fp,i,noprotocol);
	STAT(fp,i,malformed);
	if(fwprintf(fp,L"</%s>",decorator) < 0){
		return -1;
	}
	if(agg){
		agg->frames += i->frames;
		agg->truncated += i->truncated;
		agg->noprotocol += i->noprotocol;
		agg->malformed += i->malformed;
	}
	return 0;
}
#undef STAT

// not valid unless the interface came from the interfaces[] array!
static unsigned
iface_get_idx(const interface *i){
	return i - interfaces;
}

// we wouldn't naturally want to use signed integers, but that's the api...
interface *iface_by_idx(int idx){
	if(idx < 0 || (unsigned)idx >= sizeof(interfaces) / sizeof(*interfaces)){
		return NULL;
	}
	return &interfaces[idx];
}

int idx_of_iface(const interface *i){
	return i - interfaces;
}

// We don't destroy the mutex lock here; it exists for the life of the program.
// We mustn't memset() the iface blindly, or else the lock will be destroyed!
void free_iface(interface *i){
	const struct omphalos_ctx *ctx = get_octx();
	const omphalos_iface *octx = &ctx->iface;

	// Must reap thread prior to closing the fd's, lest some other thread
	// be allocated that fd, and have the packet socket thread use it.
	if(i->pmarsh){
		reap_thread(i);
		i->pmarsh = NULL;
	}
	pthread_mutex_lock(&i->lock);
	if(i->rfd >= 0){
		if(close(i->rfd)){
			diagnostic(L"Error closing %d: %s",i->rfd,strerror(errno));
		}
		i->rfd = -1;
	}
	if(i->fd >= 0){
		if(close(i->fd)){
			diagnostic(L"Error closing %d: %s",i->fd,strerror(errno));
		}
		i->fd = -1;
	}
	if(i->fd4 >= 0){
		if(close(i->fd4)){
			diagnostic(L"Error closing %d: %s",i->fd4,strerror(errno));
		}
	}
	if(i->fd6 >= 0){
		if(close(i->fd6)){
			diagnostic(L"Error closing %d: %s",i->fd6,strerror(errno));
		}
	}
	if(i->opaque && octx->iface_removed){
		octx->iface_removed(i,i->opaque);
		i->opaque = NULL;
	}
	while(i->ip6r){
		struct ip6route *r6 = i->ip6r->next;

		free(i->ip6r);
		i->ip6r = r6;
	}
	while(i->ip4r){
		struct ip4route *r4 = i->ip4r->next;

		free(i->ip4r);
		i->ip4r = r4;
	}
	timestat_destroy(&i->fps);
	timestat_destroy(&i->bps);
	free(i->topinfo.devname);
	i->topinfo.devname = NULL;
	free(i->truncbuf);
	i->truncbuf = NULL;
	free(i->name);
	i->name = NULL;
	free(i->addr);
	i->addr = NULL;
	free(i->bcast);
	i->bcast = NULL;
	cleanup_l3hosts(&i->cells);
	cleanup_l3hosts(&i->ip6hosts);
	cleanup_l3hosts(&i->ip4hosts);
	cleanup_l2hosts(&i->l2hosts);
	pthread_mutex_unlock(&i->lock);
}

void cleanup_interfaces(void){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		if(interfaces[i].name){
			diagnostic(L"Shutting down %s",interfaces[i].name);
		}
		free_iface(&interfaces[i]);
	}
}

int destroy_interfaces(void){
	unsigned i;
	int r;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		if( (r = pthread_mutex_destroy(&interfaces[i].lock)) ){
			diagnostic(L"Couldn't destroy lock on %d (%s?)",r,strerror(r));
			return -1;
		}
	}
	return 0;
}

int print_all_iface_stats(FILE *fp,interface *agg){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		const interface *iface = &interfaces[i];

		if(iface->frames){
			if(print_iface_stats(fp,iface,agg,"iface") < 0){
				return -1;
			}
		}
	}
	return 0;
}

// Interface lock must be held upon entry
// FIXME need to check and ensure they don't overlap with existing routes
int add_route4(interface *i,const uint32_t *dst,const uint32_t *via,
				const uint32_t *src,unsigned blen){
	ip4route *r,**prev;
	struct l3host *l3;
	struct l2host *l2;

	if((r = malloc(sizeof(*r))) == NULL){
		return -1;
	}
	memcpy(&r->dst,dst,sizeof(*dst));
	r->maskbits = blen;
	prev = &i->ip4r;
	// Order most-specific (largest maskbits) to least-specific (0 maskbits)
	while(*prev){
		if(r->maskbits >= (*prev)->maskbits){
			break;
		}
		prev = &(*prev)->next;
	}
	if(*prev && r->maskbits == (*prev)->maskbits && r->dst == (*prev)->dst){
		free(r);
		r = *prev;
	}else{
		r->addrs = 0;
		r->next = *prev;
		*prev = r;
	}
	if(via){
		memcpy(&r->via,via,sizeof(*via));
		r->addrs |= ROUTE_HAS_VIA;
	}
	if(src){
		l2 = lookup_l2host(i,i->addr);
		memcpy(&r->src,src,sizeof(*src));
		r->addrs |= ROUTE_HAS_SRC;
		// Set the src for any less-specific routes we contain
		// FIXME this will only work once...it won't update :/
		while( *(prev = &(*prev)->next) ){
			assert((*prev)->maskbits <= r->maskbits);
			if(!((*prev)->addrs & ROUTE_HAS_SRC)){
				(*prev)->addrs |= ROUTE_HAS_SRC;
				memcpy(&(*prev)->src,src,sizeof(*src));
			}
		}
		assert(lookup_local_l3host(i,l2,AF_INET,src));
	}
	if(r->addrs & ROUTE_HAS_VIA && r->maskbits < 32){
		if( (l3 = lookup_global_l3host(AF_INET,via)) ){
			if( (l2 = l3_getlastl2(l3)) ){
				observe_service(i,l2,l3,IPPROTO_IP,4,L"Router",NULL);
			}
		}
	}
	return 0;
}

// Interface lock must be held upon entry
int add_route6(interface *i,const uint128_t *dst,const uint128_t *via,
				const uint128_t *src,unsigned blen){
	ip6route *r,**prev;
	struct l3host *l3;
	struct l2host *l2;

	if((r = malloc(sizeof(*r))) == NULL){
		return -1;
	}
	memcpy(&r->dst,dst,sizeof(*dst));
	r->maskbits = blen;
	prev = &i->ip6r;
	// Order most-specific (largest maskbits) to least-specific (0 maskbits)
	while(*prev){
		if(r->maskbits >= (*prev)->maskbits){
			break;
		}
		prev = &(*prev)->next;
	}
	if(*prev && r->maskbits == (*prev)->maskbits && equal128(r->dst,(*prev)->dst)){
		free(r);
		r = *prev;
	}else{
		r->addrs = 0;
		r->next = *prev;
		*prev = r;
	}
	if(via){
		memcpy(&r->via,via,sizeof(*via));
		r->addrs |= ROUTE_HAS_VIA;
	}
	if(src){
		l2 = lookup_l2host(i,i->addr);
		memcpy(&r->src,src,sizeof(*src));
		r->addrs |= ROUTE_HAS_SRC;
		// Set the src for any less-specific routes we contain
		while( *(prev = &(*prev)->next) ){
			assert((*prev)->maskbits <= r->maskbits);
			if(!((*prev)->addrs & ROUTE_HAS_SRC)){
				(*prev)->addrs |= ROUTE_HAS_SRC;
				memcpy(&(*prev)->src,src,sizeof(*src));
			}
		}
		assert(lookup_local_l3host(i,l2,AF_INET6,src));
	}
	if(r->addrs & ROUTE_HAS_VIA && r->maskbits < 128){
		if( (l3 = lookup_global_l3host(AF_INET6,via)) ){
			if( (l2 = l3_getlastl2(l3)) ){
				observe_service(i,l2,l3,IPPROTO_IP,6,L"Router",NULL);
			}
		}
	}
	return 0;
}

// FIXME need to check for overlaps and intersections etc
int del_route4(interface *i,const struct in_addr *a,unsigned blen){
	ip4route *r,**prev;

	for(prev = &i->ip4r ; (r = *prev) ; prev = &r->next){
		if(r->dst == a->s_addr && r->maskbits == blen){
			*prev = r->next;
			free(r);
			return 0;
		}
	}
	return -1;
}

// FIXME need to implement
int del_route6(interface *i,const struct in6_addr *a,unsigned blen){
	ip6route *r,**prev;

	for(prev = &i->ip6r ; (r = *prev) ; prev = &r->next){
		if(!memcmp(&r->dst,&a->s6_addr,sizeof(a->s6_addr)) && r->maskbits == blen){
			*prev = r->next;
			free(r);
			return 0;
		}
	}
	return -1;
}

static inline int
ip4_in_route(const ip4route *r,uint32_t i){
	uint64_t mask = ~0llu;

	mask <<= 32 - r->maskbits;
	return (ntohl(r->dst) & mask) == (ntohl(i) & mask);
}

int is_local4(const interface *i,uint32_t ip){
	const ip4route *r;

	for(r = i->ip4r ; r ; r = r->next){
		if(ip4_in_route(r,ip)){
			return (r->via == 0);
		}
	}
	return 0;
}

static inline int
ip6_in_route(const ip6route *r,const uint128_t i){
	uint128_t dst,mask;

	memcpy(&dst,&r->dst,sizeof(dst));
	memset(&mask,0xff,sizeof(mask));
	switch(r->maskbits / 32){
		case 0:
			mask[0] = ~0lu << (32 - r->maskbits);
			mask[1] = 0lu;
			mask[2] = 0lu;
			mask[3] = 0lu;
			break;
		case 1:
			mask[1] = ~0lu << (64 - r->maskbits);
			mask[2] = 0lu;
			mask[3] = 0lu;
			break;
		case 2:
			mask[2] = ~0lu << (96 - r->maskbits);
			mask[3] = 0lu;
			break;
		case 3:
			mask[3] = ~0lu << (128 - r->maskbits);
			break;
		case 4:
			break;
	}
	return equal128(dst & mask,i & mask);
}

int is_local6(const interface *i,const struct in6_addr *a){
	const ip6route *r;

	for(r = i->ip6r ; r ; r = r->next){
		if(ip6_in_route(r,*(const uint128_t *)a->s6_addr32)){
			return 1;
		}
	}
	return 0;
}

typedef struct arptype {
	unsigned ifi_type;
	const char *name;
	analyzefxn analyze;
} arptype;

static arptype arptypes[] = {
	{
		.ifi_type = ARPHRD_LOOPBACK,
		.name = "Loopback",
		.analyze = handle_ethernet_packet, // FIXME don't search l2 tables
	},{
		.ifi_type = ARPHRD_ETHER,
		.name = "Ethernet",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_IEEE80211,
		.name = "Wireless",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_IEEE80211_RADIOTAP,
		.name = "Radiotap",
		.analyze = handle_radiotap_packet,
	},{
		.ifi_type = ARPHRD_IEEE1394,
		.name = "Firewire",			// RFC 2734 / 3146
		.analyze = handle_firewire_packet,
	},{
		.ifi_type = ARPHRD_TUNNEL,
		.name = "Tunnelv4",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_SIT,
		.name = "TunnelSIT",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_IPGRE,
		.name = "TunnelGRE",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_IRDA,
		.name = "IrDA",
		.analyze = handle_irda_packet,
	},{
		.ifi_type = ARPHRD_TUNNEL6,
		.name = "Tunnelv6",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_NONE,
		.name = "VArpless",
		.analyze = handle_ethernet_packet, // FIXME no l2 header at all
	},{
		.ifi_type = ARPHRD_VOID,
		.name = "Voiddev",
		.analyze = handle_void_packet,		// FIXME likely metadata
	},
};

const char *lookup_arptype(unsigned arphrd,analyzefxn *analyzer){
	unsigned idx;

	for(idx = 0 ; idx < sizeof(arptypes) / sizeof(*arptypes) ; ++idx){
		const arptype *at = arptypes + idx;

		if(at->ifi_type == arphrd){
			if(analyzer){
				*analyzer = at->analyze;
			}
			return at->name;
		}
	}
	return NULL;
}

int up_interface(const interface *i){
	int fd;

	if((fd = netlink_socket()) < 0){
		return -1;
	}
	if(iplink_modify(fd,iface_get_idx(i),IFF_UP,IFF_UP)){
		close(fd);
		return -1;
	}
	if(close(fd)){
		return -1;
	}
	// FIXME we're not necessarily up yet...flags won't even be updated
	// until we get the confirming netlink message. ought we block on that
	// message? spin on interrogation?
	return 0;
}

int down_interface(const interface *i){
	int fd;

	if((fd = netlink_socket()) < 0){
		return -1;
	}
	if(iplink_modify(fd,iface_get_idx(i),0,IFF_UP)){
		close(fd);
		return -1;
	}
	if(close(fd)){
		return -1;
	}
	// FIXME we're not necessarily down yet...flags won't even be updated
	// until we get the confirming netlink message. ought we block on that
	// message? spin on interrogation?
	return 0;
}

int enable_promiscuity(const interface *i){
	int fd;

	if((fd = netlink_socket()) < 0){
		return -1;
	}
	if(iplink_modify(fd,iface_get_idx(i),IFF_PROMISC,IFF_PROMISC)){
		close(fd);
		return -1;
	}
	if(close(fd)){
		return -1;
	}
	// FIXME we're not necessarily in promiscuous mode yet...flags won't
	// even be updated until we get the confirming netlink message. ought
	// we block on that message? spin on interrogation?
	return 0;
}

int disable_promiscuity(const interface *i){
	int fd;

	if((fd = netlink_socket()) < 0){
		return -1;
	}
	if(iplink_modify(fd,iface_get_idx(i),0,IFF_PROMISC)){
		close(fd);
		return -1;
	}
	if(close(fd)){
		diagnostic(L"couldn't close netlink socket %d (%s?)",fd,strerror(errno));
		return -1;
	}
	// FIXME we're not necessarily out of promiscuous mode yet...i->flags
	// won't even be updated until we get the confirming netlink message.
	// ought we block on that message? spin on interrogation?
	return 0;
}

// FIXME these need to take into account priority (table number)
// FIXME how to handle policy routing (rules)?
static const ip4route *
get_route4(const interface *i,const uint32_t *ip){
	const ip4route *i4r;

	for(i4r = i->ip4r ; i4r ; i4r = i4r->next){
		if(ip4_in_route(i4r,*ip)){
			break;
		}
	}
	return i4r;
}

static const ip6route *
get_route6(const interface *i,const void *ip){
	const ip6route *i6r;

	for(i6r = i->ip6r ; i6r ; i6r = i6r->next){
		uint128_t i;

		memcpy(&i,ip,sizeof(i));
		if(ip6_in_route(i6r,i)){
			return i6r;
		}
	}
	return NULL;
}

const void *
get_source_address(interface *i,int fam,const void *addr,void *s){
	switch(fam){
		case AF_INET:{
			const ip4route *i4r = addr ? get_route4(i,addr) : i->ip4r;

			if(i4r && (i4r->addrs & ROUTE_HAS_SRC)){
				memcpy(s,&i4r->src,sizeof(uint32_t));
			}else{
				return NULL;
			}
			break;
		}case AF_INET6:{
			const ip6route *i6r = addr ? get_route6(i,addr) : i->ip6r;

			// FIXME ipv6 routes very rarely set their src :/
			if(i6r && (i6r->addrs & ROUTE_HAS_SRC)){
				memcpy(s,&i6r->src,sizeof(uint128_t));
			}else{
				return NULL;
			}
			break;
		}default:
			return NULL;
	}
	return s;
}

// Network byte-order inputs
const void *
get_unicast_address(interface *i,const void *hwaddr,int fam,const void *addr,void *r){
	int ret = 0;

	assert(hwaddr); // FIXME
	switch(fam){
		case AF_INET:{
			const ip4route *i4r = get_route4(i,addr);

			if(i4r){
				// A routed result requires a directed ARP
				// probe to verify the local network address
				// (our idea of the route might be wrong).
				if(i4r->addrs & ROUTE_HAS_VIA){
					// FIXME even if we have a route to it,
					// check the hwaddr to see that it matches
					// the hwaddr of the router. if not, send
					// an arp probe to the original address.
					// this will detect colocated hosts
					// with different network settings.
					/*
					const ip4route *i4v;

					if( (i4v = get_route4(i,&i4r->via)) ){
						if(i4v->addrs & ROUTE_HAS_SRC){
							uint32_t v = i4v->src;
							assert(*(const uint32_t *)&i4v->src);
							send_arp_probe(i,hwaddr,
									addr,
									sizeof(uint32_t),
									&i4v->src);
						}
					}*/
				}else{
					ret = 1;
					memcpy(r,addr,sizeof(uint32_t));
				}
			}else{
				// In case we don't have a link-local address
				// configured yet, special-case them...
				if(unrouted_ip4(addr)){
					ret = 1;
					memcpy(r,addr,sizeof(uint32_t));
				}
			}
			break;
		}case AF_INET6:{
			const ip6route *i6r = get_route6(i,addr);

			if(i6r){ // FIXME handle routed addresses!
				if(!(i6r->addrs & ROUTE_HAS_VIA)){
					ret = 1;
					memcpy(r,addr,sizeof(uint128_t));
				}
			}else{
				// In case we don't have a link-local address
				// configured yet, special-case them...
				if(unrouted_ip6(addr)){
					ret = 1;
					memcpy(r,addr,sizeof(uint128_t));
				}
			}
			break;
		}default:
			break;
	}
	return (ret != 1) ? NULL : r;
}

// FIXME need support multiple addresses, and match best up with each route
void set_default_ipv6src(interface *i,const uint128_t ip){
	i->ip6defsrc = ip;
}
