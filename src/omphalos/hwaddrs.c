#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <omphalos/iana.h>
#include <linux/rtnetlink.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct l2host {
	hwaddrint hwaddr;		// hardware address
	const wchar_t *devname;		// text description based off lladdress
	struct l2host *next;
	uintmax_t srcpkts,dstpkts;	// stats
	interface *i;
	void *opaque;
} l2host;

static inline l2host *
create_l2host(interface *,const void *) __attribute__ ((malloc));

// FIXME replace internals with LRU acquisition...
// FIXME caller must set ->next
static inline l2host *
create_l2host(interface *i,const void *hwaddr){
	l2host *l2;

	if( (l2 = malloc(sizeof(*l2))) ){
		l2->dstpkts = l2->srcpkts = 0;
		l2->hwaddr = 0;
		memcpy(&l2->hwaddr,hwaddr,i->addrlen);
		l2->opaque = NULL;
		l2->i = i;
		if((i->flags & IFF_BROADCAST) && i->bcast &&
				memcmp(hwaddr,i->bcast,i->addrlen) == 0){
			l2->devname = L"Link broadcast";
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
l2host *lookup_l2host(interface *i,const void *hwaddr){
	const omphalos_ctx *octx = get_octx();
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
	l2 = create_l2host(i,hwaddr);
	assert(l2);
	if(l2){
		l2->next = i->l2hosts;
		i->l2hosts = l2;
		if(octx->iface.neigh_event){
			l2->opaque = octx->iface.neigh_event(i,l2);
		}
	}
	return l2;
}

void cleanup_l2hosts(l2host **list){
	l2host *l2, *tmp;

	for(l2 = *list ; l2 ; l2 = tmp){
		tmp = l2->next;
		free(l2);
	}
	*list = NULL;
}

void hwntop(const void *hwaddr, size_t len, char *buf){
	unsigned idx;
	size_t s;

	if(len){
		s = HWADDRSTRLEN(len);
		for(idx = 0 ; idx < len - 1 ; ++idx){
			snprintf(buf + idx * 3, s - idx * 3, "%02x:",
					((const unsigned char *)hwaddr)[idx]);
		}
    snprintf(buf + idx * 3, s - idx * 3, "%02x",
        ((const unsigned char *)hwaddr)[idx]);
	}else{
		buf[0] = '\0';
	}
}

void l2ntop(const l2host *l2, size_t len, void *buf){
	hwntop(&l2->hwaddr, len, buf);
}

char *l2addrstr(const l2host *l2){
	const size_t len = l2->i->addrlen;
	char *r;

	if( (r = malloc(HWADDRSTRLEN(len))) ){
		l2ntop(l2, len, r);
	}
	return r;
}

void *l2host_get_opaque(l2host *l2){
	return l2->opaque;
}

int l2hostcmp(const l2host *l21,const l2host *l22,size_t addrlen){
	return memcmp(&l21->hwaddr,&l22->hwaddr,addrlen);
}

int categorize_l2addr(const interface *i,const void *hwaddr){
	if(memcmp(i->addr,hwaddr,i->addrlen) == 0){
		return RTN_LOCAL;
	}
	if(i->arptype == ARPHRD_ETHER){
		if(categorize_ethaddr(hwaddr) == RTN_MULTICAST){
			if((i->flags & IFF_BROADCAST) && i->bcast){
				if(memcmp(i->bcast,hwaddr,i->addrlen) == 0){
					return RTN_BROADCAST;
				}
			}
			return RTN_MULTICAST;
		}
		return RTN_UNICAST;
	}
	if((i->flags & IFF_BROADCAST) && i->bcast){
		if(memcmp(i->bcast,hwaddr,i->addrlen) == 0){
			return RTN_BROADCAST;
		}
	}
	return RTN_UNICAST;
}

int l2categorize(const interface *i, const l2host *l2){
	return categorize_l2addr(i, &l2->hwaddr);
}

hwaddrint get_hwaddr(const l2host *l2){
	return l2->hwaddr;
}

const wchar_t *get_devname(const l2host *l2){
	return l2->devname;
}

void l2srcpkt(l2host *l2){
	++l2->srcpkts;
}

void l2dstpkt(l2host *l2){
	++l2->dstpkts;
}

uintmax_t get_srcpkts(const l2host *l2){
	return l2->srcpkts;
}

uintmax_t get_dstpkts(const l2host *l2){
	return l2->dstpkts;
}

interface *l2_getiface(l2host *l2){
	return l2->i;
}
