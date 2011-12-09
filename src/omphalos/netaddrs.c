#include <errno.h>
#include <wchar.h>
#include <stdlib.h>
#include <assert.h>
#include <omphalos/dns.h>
#include <omphalos/util.h>
#include <omphalos/diag.h>
#include <omphalos/ietf.h>
#include <omphalos/route.h>
#include <omphalos/resolv.h>
#include <omphalos/service.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

// Don't want to backoff name resolution attempts to more than 2^9s or so.
#define MAX_BACKOFF_EXP		9

typedef struct l3host {
	wchar_t *name;
	int fam;	// FIXME kill determine from addr relative to arenas
	union {
		uint32_t ip4;
		uint128_t ip6;
		char mac[ETH_ALEN];
	} addr;		// FIXME sigh
	uintmax_t srcpkts,dstpkts;
	namelevel nlevel;
	unsigned nosrvs;	// FIXME kill oughtn't be necessary
	// FIXME use usec-based ticks taken from the omphalos_packet *!
	time_t nextnametry;	// next time we can attempt name resolution
	unsigned nametries;	// number of times we've tried name resolution
	struct l4srv *services;	// services observed providing
	struct l3host *next;	// next within the interface
	struct l2host *l2;	// FIXME we only keep the most recent l2host
				// seen with this address. ought keep all, or
				// at the very least one per interface...
	void *opaque;		// UI state
	struct l3host *gnext;	// next globally
	pthread_mutex_t nlock;	// naming lock
} l3host;

static l3host external_l3 = {
	.name = L"external",
	.fam = AF_INET,
	.nosrvs = 1,
}; // FIXME augh

static struct globalhosts {
	struct l3host *head;
	pthread_mutex_t lock;
	size_t addrlen;
} ipv4hosts = {
	.head = NULL,
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.addrlen = 4,
},ipv6hosts = {
	.head = NULL,
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.addrlen = 16,
};

// RFC 3513 notes :: (all zeros) to be the "unspecified" address. It ought
// never appear as a destination address.
static l3host unspecified_ipv6 = {
	.name = L"unspec6",
	.fam = AF_INET6,
	.nosrvs = 1,
};

static l3host unspecified_ipv4 = {
	.name = L"unspec4",
	.fam = AF_INET,
	.nosrvs = 1,
};

static inline struct globalhosts *
get_global_hosts(int fam){
	struct globalhosts *ret;

	switch(fam){
		case AF_INET:
			ret = &ipv4hosts;
			break;
		case AF_INET6:
			ret = &ipv6hosts;
			break;
		default:
			ret = NULL;
			break;
	}
	if(ret){
		if(pthread_mutex_lock(&ret->lock)){
			ret = NULL;
		}
	}
	return ret;
}

// FIXME like the l2addrs, need do this in constant space via LRU or something
static l3host *
create_l3host(int fam,const void *addr,size_t len){
	l3host *r;

	assert(len <= sizeof(r->addr));
	if( (r = Malloc(sizeof(*r))) ){
		struct globalhosts *gh;
		int ret;

		if( (ret = pthread_mutex_init(&r->nlock,NULL)) ){
			diagnostic("%s couldn't initialize mutex (%s?)",
					__func__,strerror(ret));
			free(r);
			return NULL;
		}
		r->opaque = NULL;
		r->name = NULL;
		r->l2 = NULL;
		r->fam = fam;
		r->srcpkts = r->dstpkts = 0;
		r->nlevel = NAMING_LEVEL_NONE;
		r->services = NULL;
		r->nosrvs = 0;
		r->nextnametry = 0;
		r->nametries = 0;
		memcpy(&r->addr,addr,len);
		if( (gh = get_global_hosts(fam)) ){
			r->gnext = gh->head;
			gh->head = r;
			pthread_mutex_unlock(&gh->lock);
		}else{
			r->gnext = NULL;
		}
	}
	return r;
}

void name_l3host_absolute(const interface *i,
			struct l2host *l2,l3host *l3,const char *name,
			namelevel nlevel){
	wchar_t *wname;
	size_t wlen;

	if((wlen = mbstowcs(NULL,name,0)) == (size_t)-1){
		diagnostic("Couldn't normalize [%s]",name);
	}else if( (wname = Malloc((wlen + 1) * sizeof(*wname))) ){
		size_t r;

		r = mbstowcs(wname,name,wlen + 1);
		if(r == (size_t)-1){
			diagnostic("Invalid name: [%s]",name);
			return;
		}
		if(r != wlen){
			assert(r == wlen + 1);
		}
		wname_l3host_absolute(i,l2,l3,wname,nlevel);
		free(wname);
	}
}

void wname_l3host_absolute(const interface *i,
			struct l2host *l2,l3host *l3,const wchar_t *name,
			namelevel nlevel){
	pthread_mutex_lock(&l3->nlock);
	if(l3->nlevel < nlevel){
		wchar_t *tmp;

		if( (tmp = wcsdup(name)) ){
			const omphalos_ctx *octx = get_octx();

			free(l3->name);
			l3->name = tmp;
			l3->nlevel = nlevel;
			if(octx->iface.host_event){
				l3->opaque = octx->iface.host_event(i,l2,l3);
			}
		}
	}
	pthread_mutex_unlock(&l3->nlock);
}

// An interface-scoped lookup without lower-level information. It doesn't
// create a new entry if none exists. No support for BSSID lookup.
struct l3host *find_l3host(interface *i,int fam,const void *addr){
        l3host *l3;
	typeof(l3->addr) cmp;
	size_t len;

	switch(fam){
		case AF_INET:
			len = 4;
			l3 = i->ip4hosts;
			break;
		case AF_INET6:
			len = 16;
			l3 = i->ip6hosts;
			break;
		default:
			return NULL; // FIXME
	}
	assert(len <= sizeof(cmp));
	memcpy(&cmp,addr,sizeof(cmp));
	while(l3){
		if(memcmp(&l3->addr,&cmp,len) == 0){
			return l3;
		}
		l3 = l3->next;
	}
	return NULL;
}

static inline void
update_l3name(const struct timeval *tv,struct l2host *l2,l3host *l3,
		dnstxfxn dnsfxn,char *(*revstrfxn)(const void *),int cat,
		const void *addr,interface *i,int fam){
	char *rev;

	// Multicast and broadcast addresses are statically named only
	if(cat != RTN_UNICAST && cat != RTN_LOCAL){
		return;
	}
	if(l3->name && l3->nlevel > NAMING_LEVEL_NXDOMAIN){
		return;
	}else if(l3->nlevel <= NAMING_LEVEL_NXDOMAIN){
		if(tv->tv_sec <= l3->nextnametry){
			return;
		}
	}
	if(dnsfxn == NULL || revstrfxn == NULL){
		return;
	}
	if((rev = revstrfxn(addr)) == NULL){
		return;
	}
	++l3->nametries;
	// FIXME add a random factor to avoid thundering herds!
	l3->nextnametry = tv->tv_sec + (1u << (l3->nametries > MAX_BACKOFF_EXP ?
					MAX_BACKOFF_EXP : l3->nametries));
	if(queue_for_naming(i,l3,dnsfxn,rev,fam,addr)){
		wname_l3host_absolute(i,l2,l3,L"Resolution failed",NAMING_LEVEL_FAIL);
	}
	free(rev);
}

// Interface lock needs be held upon entry
static l3host *
lookup_l3host_common(const struct timeval *tv,interface *i,struct l2host *l2,
			int fam,const void *addr,int knownlocal){
	char *(*revstrfxn)(const void *);
        l3host *l3,**prev,**orig;
	typeof(l3->addr) cmp;
	dnstxfxn dnsfxn;
	size_t len;
	int cat;

	switch(fam){
		case AF_INET:{
			const uint32_t zaddr = 0;

			len = 4;
			orig = &i->ip4hosts;
			dnsfxn = tx_dns_ptr;
			revstrfxn = rev_dns_a;
			if(memcmp(addr,&zaddr,len) == 0){
				return &unspecified_ipv4;
			}
			break;
		}case AF_INET6:{
			const uint128_t zaddr = ZERO128;

			len = 16;
			orig = &i->ip6hosts;
			dnsfxn = tx_dns_ptr;
			revstrfxn = rev_dns_aaaa;
			if(memcmp(addr,&zaddr,len) == 0){
				return &unspecified_ipv6;
			}
			break;
		}case AF_BSSID:{
			len = ETH_ALEN;
			orig = &i->cells;
			dnsfxn = NULL;
			revstrfxn = NULL;
			break;
		}default:{
			diagnostic("Don't support l3 type %d",fam);
			return NULL; // FIXME
		}
	}
	cat = l2categorize(i,l2);
	if(!(i->flags & IFF_NOARP) && !knownlocal){
		if(cat == RTN_UNICAST || cat == RTN_LOCAL){
			struct sockaddr_storage ss;
			hwaddrint hwaddr = get_hwaddr(l2);
			// FIXME throwing out anything to which we have no
			// route means we basically don't work pre-config.
			// addresses pre-configuration have information, but
			// are inferior to those post-configuration. we need a
			// means of *updating* names whenever routes change,
			// or as close to true route cache behavior as we like
			if(get_unicast_address(i,&hwaddr,fam,addr,&ss) == NULL){
				return &external_l3; // FIXME terrible
			}
		}
	}
	// FIXME probably want to make this per-node
	assert(len <= sizeof(cmp));
	memcpy(&cmp,addr,len);
	for(prev = orig ; (l3 = *prev) ; prev = &l3->next){
		if(memcmp(&l3->addr,&cmp,len) == 0){
			// Move it to the front of the list, splicing it out
			*prev = l3->next;
			l3->next = *orig;
			*orig = l3;
			l3->l2 = l2; // FIXME ought indicate a change!
			update_l3name(tv,l2,l3,dnsfxn,revstrfxn,cat,addr,i,fam);
			return l3;
		}
	}
        if( (l3 = create_l3host(fam,addr,len)) ){
		char *rev;

                l3->next = *orig;
                *orig = l3;
		l3->l2 = l2;
		// handle 127.0.0.1 and ::1 as special cases, but look up local
		// addresses otherwise. multicast and broadcast are only named
		// via special case static lookups.
		if(cat == RTN_MULTICAST){
			const wchar_t *mname = ietf_multicast_lookup(fam,addr);
			if(mname){
				wname_l3host_absolute(i,l2,l3,mname,NAMING_LEVEL_GLOBAL);
			}
			return l3; // static multicast naming only
		}else if(cat == RTN_BROADCAST){
			const wchar_t *bname = ietf_bcast_lookup(fam,addr);
			if(bname){
				wname_l3host_absolute(i,l2,l3,bname,NAMING_LEVEL_GLOBAL);
			}
			return l3; // static broadcast naming only
		}else{
			const wchar_t *uname;

			if(cat == RTN_LOCAL){
				uname = ietf_local_lookup(fam,addr);
				if(uname){
					wname_l3host_absolute(i,l2,l3,uname,NAMING_LEVEL_GLOBAL);
					return l3;
				}
			} // fallthrough: look locals up if they're not special cases
		       	uname = ietf_unicast_lookup(fam,addr);
			if(dnsfxn && revstrfxn && (rev = revstrfxn(addr))){
				// Calls the host event if necessary
				wname_l3host_absolute(i,l2,l3,L"Resolving...",NAMING_LEVEL_RESOLVING);
				++l3->nextnametry;
				l3->nextnametry = tv->tv_sec + 1;
				if(queue_for_naming(i,l3,dnsfxn,rev,fam,addr)){
					wname_l3host_absolute(i,l2,l3,L"Resolution failed",NAMING_LEVEL_FAIL);
				}
				free(rev);
			}
			if(is_router(fam,addr)){
				observe_service(i,l2,l3,IPPROTO_IP,4,L"Router",NULL);
			}
		}
        }
        return l3;
}

// Browse the global list. Don't create the host if it doesn't exist. Since
// references are handed out without a lock held, we cannot destroy an l3host
// which is on the global list! This is fundamentally unsafe, really FIXME.
struct l3host *lookup_global_l3host(int fam,const void *addr){
	struct globalhosts *gh;
	l3host *l3;
	typeof(l3->addr) cmp;

	// locks the globalhosts entry on success
	if((gh = get_global_hosts(fam)) == NULL){
		return NULL;
	}
	assert(gh->addrlen <= sizeof(cmp));
	memcpy(&cmp,addr,gh->addrlen);
	for(l3 = gh->head ; l3 ; l3 = l3->gnext){
		if(memcmp(&l3->addr,&cmp,gh->addrlen) == 0){
			break;
		}
	}
	pthread_mutex_unlock(&gh->lock);
	return l3;
}

// This is for raw network addresses as seen on the wire, which may be from
// outside the local network. We want only the local network address(es) of the
// link (in a rare case, it might not have any). For unicast link addresses, a
// route lookup will be performed using the wire network address. If the route
// returned is different from the wire address, an ARP probe is directed to the
// link-layer address (this is all handled by get_route()). ARP replies are
// link-layer only, and thus processed directly (name_l2host_local()).
l3host *lookup_l3host(const struct timeval *tv,interface *i,struct l2host *l2,
				int fam,const void *addr){
	struct timeval t;

	if(tv){
		t = *tv;
	}else{
		gettimeofday(&t,NULL);
	}
	return lookup_l3host_common(&t,i,l2,fam,addr,0);
}

l3host *lookup_local_l3host(const struct timeval *tv,interface *i,
			struct l2host *l2,int fam,const void *addr){
	struct timeval t;

	if(tv){
		t = *tv;
	}else{
		gettimeofday(&t,NULL);
	}
	return lookup_l3host_common(&t,i,l2,fam,addr,1);
}

void name_l3host_local(const interface *i,struct l2host *l2,l3host *l3,int family,const void *name,
		namelevel nlevel){
	char b[INET6_ADDRSTRLEN];

	if(inet_ntop(family,name,b,sizeof(b)) == b){
		name_l3host_absolute(i,l2,l3,b,nlevel);
	}
}

char *l3addrstr(const struct l3host *l3){
	const size_t len = INET6_ADDRSTRLEN + 1;
	char *buf;

	if( (buf = Malloc(len)) ){
		assert(l3ntop(l3,buf,len) == 0);
	}
	return buf;
}

char *netaddrstr(int fam,const void *addr){
	const size_t len = INET6_ADDRSTRLEN + 1;
	char *buf;

	if( (buf = Malloc(len)) ){
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

uint32_t get_l3addr_in(const l3host *l3){
	return l3->addr.ip4;
}

const uint128_t *get_l3addr_in6(const l3host *l3){
	return &l3->addr.ip6;
}

const wchar_t *get_l3name(const l3host *l3){
	return l3->name;
}

namelevel get_l3nlevel(const l3host *l3){
	return l3->nlevel;
}

void *l3host_get_opaque(l3host *l3){
	return l3->opaque;
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
		pthread_mutex_destroy(&l3->nlock);
		free_services(l3->services);
		free(l3->name);
		free(l3);
	}
	*list = NULL;
	// FIXME ensure all globall3host lists are empty
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

struct l2host *l3_getlastl2(l3host *l3){
	return l3->l2;
}

struct l4srv *l3_getservices(l3host *l3){
	return l3->services;
}

const struct l4srv *l3_getconstservices(const l3host *l3){
	return l3->services;
}

int l3_setservices(l3host *l3,struct l4srv *l4){
	if(!l3->nosrvs){
		l3->services = l4;
		return 0;
	}
	return -1;
}
