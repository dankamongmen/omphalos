#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <omphalos/util.h>
#include <omphalos/ietf.h>
#include <omphalos/resolv.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

typedef struct l3host {
	char *name;
	int fam;	// FIXME kill determine from addr relative to arenas
	int path;	// does it provide access elsewhere? (route/AP)
	union {
		uint32_t ip4;
		uint128_t ip6;
		char mac[ETH_ALEN];
	} addr;		// FIXME sigh
	uintmax_t srcpkts,dstpkts;
	namelevel nlevel;
	struct l3host *next;
	void *opaque;
} l3host;

static l3host external_l3 = {
	.name = "external",
	.fam = AF_INET,
}; // FIXME augh

// FIXME like the l2addrs, need do this in constant space via LRU or something
static l3host *
create_l3host(int fam,const void *addr,size_t len){
	l3host *r;

	assert(len <= sizeof(r->addr));
	if( (r = malloc(sizeof(*r))) ){
		r->opaque = NULL;
		r->name = NULL;
		r->fam = fam;
		r->path = 0;
		r->srcpkts = r->dstpkts = 0;
		r->nlevel = NAMING_LEVEL_NONE;
		memcpy(&r->addr,addr,len);
	}
	return r;
}

void name_l3host_absolute(const omphalos_iface *octx,const interface *i,
			struct l2host *l2,l3host *l3,const char *name,
			namelevel nlevel){
	if(l3->nlevel < nlevel){
		char *tmp;

		if( (tmp = strdup(name)) ){
			free(l3->name);
			l3->name = tmp;
			l3->nlevel = nlevel;
			if(octx->host_event){
				l3->opaque = octx->host_event(i,l2,l3);
			}
		}
	}
}

static inline int
routed_family_p(int fam){
	return fam == AF_INET || fam == AF_INET6;
}

// This is for raw network addresses as seen on the wire, which may be from
// outside the local network. We want only the local network address(es) of the
// link (in a rare case, it might not have any). For unicast link addresses, a
// route lookup will be performed using the wire network address. If the route
// returned is different from the wire address, an ARP probe is directed to the
// link-layer address (this is all handled by get_route()). ARP replies are
// link-layer only, and thus processed directly (name_l2host_local()).
struct l3host *lookup_l3host(const omphalos_iface *octx,interface *i,
				struct l2host *l2,int fam,const void *addr){
        l3host *l3,**prev,**orig;
	typeof(l3->addr) cmp;
	size_t len;
	int cat;

	switch(fam){
		case AF_INET:
			len = 4;
			orig = &i->ip4hosts;
			break;
		case AF_INET6:
			len = 16;
			orig = &i->ip6hosts;
			break;
		case AF_BSSID:
			len = ETH_ALEN;
			orig = &i->cells;
			break;
		default:
			octx->diagnostic("Can't lookup l3 type %d",fam);
			return NULL; // FIXME
	}
	if(routed_family_p(fam)){
		if((cat = l2categorize(i,l2)) == RTN_UNICAST){
			struct sockaddr_storage ss;
			hwaddrint hwaddr = get_hwaddr(l2);
			// FIXME throwing out anything to which we have no
			// route means we basically don't work pre-config.
			// addresses pre-configuration have information, but
			// are inferior to those post-configuration. we need a
			// means of *updating* names whenever routes change,
			// or as close to true route cache behavior as we like
			if(get_unicast_address(octx,i,&hwaddr,fam,addr,&ss) == NULL){
				return &external_l3; // FIXME terrible
			}
		}
	}else{
		cat = RTN_UNICAST;
	}
	// FIXME probably want to make this per-node
	// FIXME only track local addresses, not those we route to
	assert(len <= sizeof(cmp));
	memcpy(&cmp,addr,sizeof(cmp));
	for(prev = orig ; (l3 = *prev) ; prev = &l3->next){
		if(memcmp(&l3->addr,&cmp,len) == 0){
			// Move it to the front of the list, splicing it out
			*prev = l3->next;
			l3->next = *orig;
			*orig = l3;
			return l3;
		}
	}
        if( (l3 = create_l3host(fam,addr,len)) ){
                l3->next = *orig;
                *orig = l3;
		if(fam == AF_BSSID){
			l3->path = 1;
		}
		if(cat == RTN_MULTICAST){
			const char *mname = ietf_multicast_lookup(fam,addr);
			if(mname){
				l3->name = strdup(mname);
			}
		}else if(cat == RTN_UNICAST || cat == RTN_LOCAL){
			queue_for_naming(i,l2,l3);
		}else{
			// FIXME do what with broadcast?
		}
		if(octx->host_event){
			l3->opaque = octx->host_event(i,l2,l3);
		}
        }
        return l3;
}

void name_l3host_local(const omphalos_iface *octx,const interface *i,
		struct l2host *l2,l3host *l3,int family,const void *name,
		namelevel nlevel){
	if(l3->name == NULL){
		char b[INET6_ADDRSTRLEN];

		if(inet_ntop(family,name,b,sizeof(b)) == b){
			name_l3host_absolute(octx,i,l2,l3,b,nlevel);
		}
	}
}

char *l3addrstr(const struct l3host *l3){
	const size_t len = INET6_ADDRSTRLEN + 1;
	char *buf;

	if( (buf = malloc(len)) ){
		assert(l3ntop(l3,buf,len) == 0);
	}
	return buf;
}

char *netaddrstr(int fam,const void *addr){
	const size_t len = INET6_ADDRSTRLEN + 1;
	char *buf;

	if( (buf = malloc(len)) ){
		if(inet_ntop(fam,addr,buf,len) != buf){
			free(buf);
			buf = NULL;
		}
	}
	return buf;
}

int l3addr_eq_p(const l3host *l3,int fam,const void *addr){
	if(l3->fam != fam){
		return 0;
	}else if(fam == AF_INET){
		return !memcmp(&l3->addr.ip4,addr,4);
	}else if(fam == AF_INET6){
		return !memcmp(&l3->addr.ip6,addr,16);
	}
	return 0;
}

const char *get_l3name(const l3host *l3){
	return l3->name;
}

void *l3host_get_opaque(l3host *l3){
	return l3->opaque;
}

int router_p(const l3host *l3){
	return l3->path;
}

int l3ntop(const l3host *l3,char *buf,size_t buflen){
	if(l3->fam > AF_MAX){
		if(buflen < HWADDRSTRLEN(ETH_ALEN)){
			errno = ENOSPC;
			return -1;
		}
		hwntop(l3->addr.mac,ETH_ALEN,buf);
		return 0;
	}
	return inet_ntop(l3->fam,&l3->addr,buf,buflen) != buf;
}

void cleanup_l3hosts(l3host **list){
	l3host *l3,*tmp;

	for(l3 = *list ; l3 ; l3 = tmp){
		tmp = l3->next;
		free(l3->name);
		free(l3);
	}
	*list = NULL;
}

void l3_srcpkt(l3host *l3){
	++l3->srcpkts;
}

void l3_dstpkt(l3host *l3){
	++l3->dstpkts;
}

uintmax_t l3_get_srcpkt(const l3host *l3){
	return l3->srcpkts;
}

uintmax_t l3_get_dstpkt(const l3host *l3){
	return l3->dstpkts;
}
