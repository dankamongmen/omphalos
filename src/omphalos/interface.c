#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netlink.h>
#include <omphalos/omphalos.h>
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
	if(i->fd >= 0){
		reap_thread(octx,i->tid);
		close(i->fd);
	}
	if(i->rfd >= 0){
		close(i->rfd);
	}
	free(i->name);
	free(i->addr);
	cleanup_l2hosts(&i->l2hosts);
	memset(i,0,sizeof(*i));
	i->rfd = i->fd = -1;
}

void cleanup_interfaces(const omphalos_iface *pctx){
	unsigned i;

	for(i = 0 ; i < sizeof(interfaces) / sizeof(*interfaces) ; ++i){
		if(interfaces[i].opaque && pctx->iface_removed){
			pctx->iface_removed(&interfaces[i],interfaces[i].opaque);
		}
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
	},{
		.ifi_type = ARPHRD_IEEE80211_RADIOTAP,
		.name = "Radiotap",
	},{
		.ifi_type = ARPHRD_TUNNEL,
		.name = "Tunnelv4",
	},{
		.ifi_type = ARPHRD_TUNNEL6,
		.name = "TunnelV6",
	},{
		.ifi_type = ARPHRD_NONE,
		.name = "VArpless",
	},
};

const char *lookup_arptype(unsigned arphrd){
	unsigned idx;

	for(idx = 0 ; idx < sizeof(arptypes) / sizeof(*arptypes) ; ++idx){
		const arptype *at = arptypes + idx;

		if(at->ifi_type == arphrd){
			return at->name;
		}
	}
	return NULL;
}
