#include <stdlib.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

typedef struct l3host {
	char *name;
	struct l3host *next;
} l3host;

struct l3host *lookup_l3host(const interface *i,int fam,const void *name){
	// FIXME for now, everyone gets one! muh wa hahahah
	static l3host l3;

	assert(i && fam && name); // FIXME
	return &l3;
}

static inline void
name_l3host_absolute(const omphalos_iface *octx,const interface *i,
			struct l2host *l2,l3host *l3,const char *name){
	if( (l3->name = Malloc(octx,strlen(name) + 1)) ){
		strcpy(l3->name,name);
	}
	if(octx->neigh_event){
		octx->neigh_event(i,l2);
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
			// Look up family-appropriate multicast names
		}
		name_l3host_local(octx,i,l2,l3,family,name);
	}
}
