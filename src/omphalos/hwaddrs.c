#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct l2host {
	uint64_t hwaddr;	// does anything have more than 64 bits at L2?
	char *name;		// some textual description
	struct l2host *next;
	void *opaque;		// FIXME not sure about how this is being done
} l2host;

// FIXME replace internals with LRU acquisition...
static inline l2host *
create_l2host(const void *hwaddr,size_t addrlen){
	l2host *l2;

	if(addrlen > sizeof(l2->hwaddr)){
		return NULL;
	}
	if( (l2 = malloc(sizeof(*l2))) ){
		l2->hwaddr = 0;
		memcpy(&l2->hwaddr,hwaddr,addrlen);
		l2->name = NULL;
		l2->opaque = NULL;
	}
	return l2;
}

// FIXME strictly proof-of-concept. we'll want a trie- or hash-based
// lookup, backed by an arena-allocated LRU, etc...
l2host *lookup_l2host(const omphalos_iface *octx,interface *i,
			const void *hwaddr,size_t addrlen){
	l2host *l2,**prev;
	uint64_t hwcmp;

	hwcmp = 0;
	memcpy(&hwcmp,hwaddr,addrlen);
	for(prev = &i->l2hosts ; (l2 = *prev) ; prev = &l2->next){
		if(l2->hwaddr == hwcmp){
			// Move it to the front of the list, splicing it out
			*prev = l2->next;
			l2->next = i->l2hosts;
			i->l2hosts = l2;
			return l2;
		}
	}
	if( (l2 = create_l2host(hwaddr,addrlen)) ){
		if(octx->neigh_event){
			l2->opaque = octx->neigh_event(i,l2);
		}
		l2->next = i->l2hosts;
		i->l2hosts = l2;
	}
	return l2;
}

void cleanup_l2hosts(l2host **list){
	l2host *l2,*tmp;

	for(l2 = *list ; l2 ; l2 = tmp){
		free(l2->name);
		tmp = l2->next;
		free(l2);
	}
	*list = NULL;
}

char *l2addrstr(const l2host *l2,size_t len){
	unsigned idx;
	size_t s;
	char *r;

	// Each byte becomes two ASCII characters and either a separator or a nul
	s = len * 3;
	if( (r = malloc(s)) ){
		for(idx = 0 ; idx < len ; ++idx){
			snprintf(r + idx * 3,s - idx * 3,"%02x:",((unsigned char *)&l2->hwaddr)[idx]);
		}
	}
	return r;
}

int print_l2hosts(FILE *fp,const l2host *list){
	const l2host *l2;

	if( (l2 = list) ){
		if(fprintf(fp,"<neighbors>") < 0){
			return -1;
		}
		do{
			int ethtype = categorize_ethaddr(&l2->hwaddr);
			char *hwaddr = NULL;

			switch(ethtype){
			case RTN_BROADCAST:{
				if(fprintf(fp,"<ieee802 broadcast/>") < 0){
					return -1;
				}
				break;
			}case RTN_MULTICAST:{
				hwaddr = l2addrstr(l2,IFHWADDRLEN);

				if(fprintf(fp,"<ieee802 mcast=\"%s\"/>",hwaddr) < 0){
					free(hwaddr);
					return -1;
				}
				break;
			}case RTN_UNICAST:{
				hwaddr = l2addrstr(l2,IFHWADDRLEN);

				if(fprintf(fp,"<ieee802 addr=\"%s\"/>",hwaddr) < 0){
					free(hwaddr);
					return -1;
				}
				break;
			}default:{
				fprintf(stderr,"Unknown ethtype: %d\n",ethtype);
				return -1;
			}
			}
			free(hwaddr);
		}while( (l2 = l2->next) );
		if(fprintf(fp,"</neighbors>") < 0){
			return -1;
		}
	}
	return 0;
}

void *l2host_get_opaque(l2host *l2){
	return l2->opaque;
}

int l2hostcmp(const l2host *l21,const l2host *l22){
	return memcmp(&l21->hwaddr,&l22->hwaddr,IFHWADDRLEN); // FIXME len-param
}

int l2categorize(const interface *i,const l2host *l2){
	int ret;

	ret = categorize_ethaddr(&l2->hwaddr);
	if(ret == RTN_UNICAST){
		return memcmp(i->addr,&l2->hwaddr,i->addrlen) ? RTN_UNICAST : RTN_LOCAL;
	}
	return ret;
}

void name_l2host(const omphalos_iface *octx,const interface *i,l2host *l2,
					int family,const void *name){
	if(l2 && l2->name == NULL){
		struct sockaddr_storage ss;
		char b[INET6_ADDRSTRLEN];

		if(categorize_ethaddr(&l2->hwaddr) == RTN_UNICAST){
			// FIXME need to check that the name we get actually
			// responds to ARP requests at *this* l2 address.
			// otherwise, we get things like 0.0.0.0 looked up on
			// the default route and thus given the gateway addr.
			// FIXME throwing out anything to which we have no
			// route means we basically don't work pre-config.
			// addresses pre-configuration have information, but
			// are inferior to those post-configuration. we need a
			// means of *updating* names whenever routes change,
			// or as close to true route cache behavior as we like
			if((name = get_route(i,family,name,&ss)) == NULL){
				return;
			}
		}
		assert(inet_ntop(family,name,b,sizeof(b)) == b);
		if( (l2->name = malloc(strlen(b) + 1)) ){
			strcpy(l2->name,b);
		}
		if(octx->neigh_event){
			octx->neigh_event(i,l2);
		}
	}
}

const char *get_name(const l2host *l2){
	return l2->name;
}
