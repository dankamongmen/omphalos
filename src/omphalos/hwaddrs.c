#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <linux/if_arp.h>
#include <omphalos/iana.h>
#include <linux/rtnetlink.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct l2host {
	hwaddrint hwaddr;
	const char *devname;	// description based off lladdress
	struct l2host *next;
	void *opaque;		// FIXME not sure about how this is being done
} l2host;

// FIXME replace internals with LRU acquisition...
// FIXME caller must set ->next
static inline l2host *
create_l2host(const interface *i,const void *hwaddr){
	l2host *l2;

	if( (l2 = malloc(sizeof(*l2))) ){
		l2->hwaddr = 0;
		memcpy(&l2->hwaddr,hwaddr,i->addrlen);
		l2->opaque = NULL;
		if((i->flags & IFF_BROADCAST) && i->bcast &&
				memcmp(hwaddr,i->bcast,i->addrlen) == 0){
			l2->devname = "Link broadcast";
		}else if(i->arptype == ARPHRD_ETHER || i->arptype == ARPHRD_IEEE80211_RADIOTAP
				|| i->arptype == ARPHRD_IEEE80211 || i->arptype == ARPHRD_IEEE80211_PRISM){
			l2->devname = iana_lookup(hwaddr,i->addrlen);
		}else{
			l2->devname = NULL;
		}
	}
	return l2;
}

// FIXME strictly proof-of-concept. we'll want a trie- or hash-based
// lookup, backed by an arena-allocated LRU, etc...
l2host *lookup_l2host(const omphalos_iface *octx,interface *i,const void *hwaddr){
	l2host *l2,**prev;
	hwaddrint hwcmp;

	hwcmp = 0;
	memcpy(&hwcmp,hwaddr,i->addrlen);
	for(prev = &i->l2hosts ; (l2 = *prev) ; prev = &l2->next){
		if(l2->hwaddr == hwcmp){
			// Move it to the front of the list, splicing it out
			*prev = l2->next;
			l2->next = i->l2hosts;
			i->l2hosts = l2;
			return l2;
		}
	}
	if( (l2 = create_l2host(i,hwaddr)) ){
		l2->next = i->l2hosts;
		i->l2hosts = l2;
		if(octx->neigh_event){
			l2->opaque = octx->neigh_event(i,l2);
		}
	}
	return l2;
}

void cleanup_l2hosts(l2host **list){
	l2host *l2,*tmp;

	for(l2 = *list ; l2 ; l2 = tmp){
		tmp = l2->next;
		free(l2);
	}
	*list = NULL;
}

void l2ntop(const l2host *l2,size_t len,void *buf){
	unsigned idx;
	size_t s;

	s = HWADDRSTRLEN(len);
	for(idx = 0 ; idx < len ; ++idx){
		snprintf((char *)buf + idx * 3,s - idx * 3,"%02x:",
				((unsigned char *)&l2->hwaddr)[idx]);
	}
}

char *l2addrstr(const l2host *l2){
	// FIXME make generic!
	const size_t len = IFHWADDRLEN;
	char *r;

	if( (r = malloc(HWADDRSTRLEN(len))) ){
		l2ntop(l2,len,r);
	}
	return r;
}

void *l2host_get_opaque(l2host *l2){
	return l2->opaque;
}

int l2hostcmp(const l2host *l21,const l2host *l22,size_t addrlen){
	return memcmp(&l21->hwaddr,&l22->hwaddr,addrlen);
}

int l2categorize(const interface *i,const l2host *l2){
	int ret;

	ret = categorize_ethaddr(&l2->hwaddr);
	if(ret == RTN_UNICAST){
		return memcmp(i->addr,&l2->hwaddr,i->addrlen) ? RTN_UNICAST : RTN_LOCAL;
	}else if(ret == RTN_MULTICAST){
		if((i->flags & IFF_BROADCAST) && i->bcast){
			return memcmp(i->bcast,&l2->hwaddr,i->addrlen) ? RTN_MULTICAST : RTN_BROADCAST;
		}
		return RTN_MULTICAST;
	}
	return ret;
}

hwaddrint get_hwaddr(const l2host *l2){
	return l2->hwaddr;
}

const char *get_devname(const l2host *l2){
	return l2->devname;
}
