#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct l2host {
	uint64_t hwaddr;	// does anything have more than 64 bits at L2?
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
		l2->opaque = NULL;
	}
	return l2;
}

#include <assert.h>
// FIXME strictly proof-of-concept. we'll want a trie- or hash-based
// lookup, backed by an arena-allocated LRU, etc...
l2host *lookup_l2host(l2host **list,const void *hwaddr,size_t addrlen){
	l2host *l2,**prev;
	uint64_t hwcmp;

	hwcmp = 0;
	memcpy(&hwcmp,hwaddr,addrlen);
	for(prev = list ; (l2 = *prev) ; prev = &l2->next){
		if(l2->hwaddr == hwcmp){
			*prev = l2->next;
			l2->next = *list;
			*list = l2;
			return l2;
		}
	}
	if( (l2 = create_l2host(hwaddr,addrlen)) ){
		l2->next = *list;
		*list = l2;
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

int print_neigh(const interface *iface,const l2host *l2){
	char *hwaddr;
	int n;

	// FIXME need real family! inet_ntop(nd->ndm_family,l2->hwaddr,str,sizeof(str));
	hwaddr = l2addrstr(l2,IFHWADDRLEN);

	n = printf("[%8s] neighbor %s\n",iface->name,hwaddr);
	free(hwaddr);
	/* FIXME printf("[%8s] neighbor %s %s%s%s%s%s%s%s%s\n",iface->name,str,
			nd->ndm_state & NUD_INCOMPLETE ? "INCOMPLETE" : "",
			nd->ndm_state & NUD_REACHABLE ? "REACHABLE" : "",
			nd->ndm_state & NUD_STALE ? "STALE" : "",
			nd->ndm_state & NUD_DELAY ? "DELAY" : "",
			nd->ndm_state & NUD_PROBE ? "PROBE" : "",
			nd->ndm_state & NUD_FAILED ? "FAILED" : "",
			nd->ndm_state & NUD_NOARP ? "NOARP" : "",
			nd->ndm_state & NUD_PERMANENT ? "PERMANENT" : ""
			);
		*/
	return n;
}

void *l2host_get_opaque(l2host *l2){
	return l2->opaque;
}

void l2host_set_opaque(l2host *l2,void *opaque){
	l2->opaque = opaque;
}
