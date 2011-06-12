#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netlink.h>
#include <omphalos/psocket.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/interface.h>

#define MAXINTERFACES (1u << 16) // lame FIXME

static interface interfaces[MAXINTERFACES];

int init_interfaces(void){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		interface *iface = &interfaces[i];

		iface->rfd = iface->fd = -1;
	}
	return 0;
}

#define STAT(fp,i,x) if((i)->x) { if(fprintf((fp),"<"#x">%ju</"#x">",(i)->x) < 0){ return -1; } }
int print_iface_stats(FILE *fp,const interface *i,interface *agg,const char *decorator){
	if(i->name == NULL){
		if(fprintf(fp,"<%s>",decorator) < 0){
			return -1;
		}
	}else{
		if(fprintf(fp,"<%s name=\"%s\">",decorator,i->name) < 0){
			return -1;
		}
	}
	STAT(fp,i,frames);
	STAT(fp,i,truncated);
	STAT(fp,i,noprotocol);
	STAT(fp,i,malformed);
	if(fprintf(fp,"</%s>",decorator) < 0){
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

char *hwaddrstr(const interface *i){
	return l2addrstr(i->addr,i->addrlen);
}

void free_iface(const omphalos_iface *octx,interface *i){
	// Must reap thread prior to closing the fd's, lest some other thread
	// be allocated that fd, and have the packet socket thread use it.
	if(i->pmarsh){
		reap_thread(octx,i);
	}
	if(i->rfd >= 0){
		if(close(i->rfd)){
			octx->diagnostic("Error closing %d: %s",i->rfd,strerror(errno));
		}
	}
	if(i->fd >= 0){
		if(close(i->fd)){
			octx->diagnostic("Error closing %d: %s",i->fd,strerror(errno));
		}
	}
	if(i->opaque && octx->iface_removed){
		octx->iface_removed(i,i->opaque);
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
	free(i->truncbuf);
	free(i->name);
	free(i->addr);
	cleanup_l2hosts(&i->l2hosts);
	memset(i,0,sizeof(*i));
	i->rfd = i->fd = -1;
}

void cleanup_interfaces(const omphalos_iface *pctx){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		free_iface(pctx,&interfaces[i]);
	}
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

// FIXME need to check and ensure they don't overlap with existing routes
int add_route4(interface *i,const struct in_addr *s,const struct in_addr *via,
						unsigned blen,int iif){
	ip4route *r;

	if((r = malloc(sizeof(*r))) == NULL){
		return -1;
	}
	memcpy(&r->dst,s,sizeof(*s));
	if(via){
		memcpy(&r->via,via,sizeof(*via));
	}
	r->iif = iif;
	r->maskbits = blen;
	r->next = i->ip4r;
	i->ip4r = r;
	return 0;
}

int add_route6(interface *i,const struct in6_addr *s,const struct in6_addr *via,
						unsigned blen,int iif){
	ip6route *r;

	if((r = malloc(sizeof(*r))) == NULL){
		return -1;
	}
	memcpy(&r->dst,s,sizeof(*s));
	if(via){
		memcpy(&r->via,via,sizeof(*via));
	}
	r->iif = iif;
	r->maskbits = blen;
	r->next = i->ip6r;
	i->ip6r = r;
	return 0;
}

// FIXME need to check for overlaps and intersections etc
int del_route4(interface *i,const struct in_addr *a,unsigned blen){
	ip4route *r,**prev;

	for(prev = &i->ip4r ; (r = *prev) ; prev = &r->next){
		if(r->dst.s_addr == a->s_addr && r->maskbits == blen){
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
		if(!memcmp(&r->dst.s6_addr,&a->s6_addr,sizeof(a->s6_addr)) && r->maskbits == blen){
			*prev = r->next;
			free(r);
			return 0;
		}
	}
	return -1;
}

static inline int
ip4_in_route(const ip4route *r,uint32_t i){
	uint32_t mask = ~0U;

	mask <<= 32 - r->maskbits;
	return (r->dst.s_addr & mask) == (i & mask);
}

int is_local4(const interface *i,uint32_t ip){
	const ip4route *r;

	for(r = i->ip4r ; r ; r = r->next){
		if(ip4_in_route(r,ip)){
			return (r->via.s_addr == 0);
		}
	}
	return 0;
}

static inline int
ip6_in_route(const ip6route *r,const uint32_t *i){
	if(!r || !i){
		return 0;
	}
	return 1; // FIXME
}

int is_local6(const interface *i,const struct in6_addr *a){
	const ip6route *r;

	for(r = i->ip6r ; r ; r = r->next){
		if(ip6_in_route(r,a->s6_addr32)){
			return 1;
		}
	}
	return 0;
}

typedef struct arptype {
	unsigned ifi_type;
	const char *name;
	void (*analyze)(const struct omphalos_iface *,interface *,const void *,size_t);
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
		.ifi_type = ARPHRD_TUNNEL,
		.name = "Tunnelv4",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_TUNNEL6,
		.name = "TunnelV6",
		.analyze = handle_ethernet_packet,
	},{
		.ifi_type = ARPHRD_NONE,
		.name = "VArpless",
		.analyze = handle_ethernet_packet, // FIXME no l2 header at all
	},
};

const char *lookup_arptype(unsigned arphrd,void (**analyzer)(const struct omphalos_iface *,interface *,const void *,size_t)){
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

int up_interface(const omphalos_iface *octx,const interface *i){
	int fd;

	if((fd = netlink_socket(octx)) < 0){
		return -1;
	}
	if(iplink_modify(octx,fd,iface_get_idx(i),IFF_UP,IFF_UP)){
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

int down_interface(const omphalos_iface *octx,const interface *i){
	int fd;

	if((fd = netlink_socket(octx)) < 0){
		return -1;
	}
	if(iplink_modify(octx,fd,iface_get_idx(i),0,IFF_UP)){
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

int enable_promiscuity(const omphalos_iface *octx,const interface *i){
	int fd;

	if((fd = netlink_socket(octx)) < 0){
		return -1;
	}
	if(iplink_modify(octx,fd,iface_get_idx(i),IFF_PROMISC,IFF_PROMISC)){
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

int disable_promiscuity(const omphalos_iface *octx,const interface *i){
	int fd;

	if((fd = netlink_socket(octx)) < 0){
		return -1;
	}
	if(iplink_modify(octx,fd,iface_get_idx(i),0,IFF_PROMISC)){
		close(fd);
		return -1;
	}
	if(close(fd)){
		octx->diagnostic("couldn't close netlink socket %d (%s?)",fd,strerror(errno));
		return -1;
	}
	// FIXME we're not necessarily out of promiscuous mode yet...i->flags
	// won't even be updated until we get the confirming netlink message.
	// ought we block on that message? spin on interrogation?
	return 0;
}
