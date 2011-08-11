#include <stdlib.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

typedef struct l3host {
	char *name;
	int fam;	// FIXME kill determine from addr relative to arenas
	uint32_t addr;	// FIXME use something appropriate to size
	struct l3host *next;
	void *opaque;
} l3host;

// FIXME like the l2addrs, need do this in constant space via LRU or something
static l3host *
create_l3host(int fam,const void *addr,size_t len){
	l3host *r;

	if( (r = malloc(sizeof(*r))) ){
		r->opaque = NULL;
		r->name = NULL;
		r->fam = fam;
		memcpy(&r->addr,addr,len);
	}
	return r;
}

struct l3host *lookup_l3host(const omphalos_iface *octx,interface *i,
				struct l2host *l2,int fam,const void *addr){
        l3host *l3,**prev;
	uint32_t cmp;
	size_t len;

	assert(i); // FIXME
	switch(fam){
		case AF_INET:
			len = 4;
			break;
			/* FIXME
		case AF_INET6:
			len = 32;
			break;
			*/
		default:
			return NULL; // FIXME
	}
	memcpy(&cmp,addr,sizeof(cmp));
        for(prev = &i->l3hosts ; (l3 = *prev) ; prev = &l3->next){
                if(l3->addr == cmp){
                        // Move it to the front of the list, splicing it out
                        *prev = l3->next;
                        l3->next = i->l3hosts;
                        i->l3hosts = l3;
                        return l3;
                }
        }
        if( (l3 = create_l3host(fam,addr,len)) ){
                l3->next = i->l3hosts;
                i->l3hosts = l3;
                if(octx->host_event){
                        l3->opaque = octx->host_event(i,l2,l3);
                }
        }
        return l3;
}

static inline void
name_l3host_absolute(const omphalos_iface *octx,const interface *i,
			struct l2host *l2,l3host *l3,const char *name){
	if( (l3->name = Malloc(octx,strlen(name) + 1)) ){
		strcpy(l3->name,name);
		if(octx->host_event){
			octx->host_event(i,l2,l3);
		}
	}
}

void name_l3host_local(const omphalos_iface *octx,const interface *i,
		struct l2host *l2,l3host *l3,int family,const void *name){
	if(l3->name == NULL){
		char b[INET6_ADDRSTRLEN];

		if(inet_ntop(family,name,b,sizeof(b)) == b){
			name_l3host_absolute(octx,i,l2,l3,b);
		}
	}
}

// This is for raw network addresses as seen on the wire, which may be from
// outside the local network. We want only the local network address(es) of the
// link (in a rare case, it might not have any). For unicast link addresses, a
// route lookup will be performed using the wire network address. If the route
// returned is different from the wire address, an ARP probe is directed to the
// link-layer address (this is all handled by get_route()). ARP replies are
// link-layer only, and thus processed directly (name_l2host_local()).
void name_l3host(const omphalos_iface *octx,interface *i,struct l2host *l2,
				l3host *l3,int family,const void *name){
	assert(i->addrlen == ETH_ALEN); // FIXME
	if(l3->name == NULL){
		struct sockaddr_storage ss;
		hwaddrint hwaddr;
		int cat;

		hwaddr = get_hwaddr(l2);
		if((cat = categorize_ethaddr(&hwaddr)) == RTN_UNICAST){
			// FIXME throwing out anything to which we have no
			// route means we basically don't work pre-config.
			// addresses pre-configuration have information, but
			// are inferior to those post-configuration. we need a
			// means of *updating* names whenever routes change,
			// or as close to true route cache behavior as we like
			if((name = get_route(octx,i,&hwaddr,family,name,&ss)) == NULL){
				return;
			}
		}else if(cat == RTN_MULTICAST){
			// FIXME Look up family-appropriate multicast names
		}
		name_l3host_local(octx,i,l2,l3,family,name);
	}
}

char *l3addrstr(const struct l3host *l3){
	const size_t len = INET6_ADDRSTRLEN + 1;
	char *buf;

	if( (buf = malloc(len)) ){
		inet_ntop(l3->fam,&l3->addr,buf,len);
	}
	return buf;
}

const char *get_l3name(const l3host *l3){
	return l3->name;
}

void *l3host_get_opaque(l3host *l3){
	return l3->opaque;
}

int l3ntop(const l3host *l3,char *buf,size_t buflen){
	return inet_ntop(l3->fam,&l3->addr,buf,buflen) != buf;
}
