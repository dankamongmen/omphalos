#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <net/if.h>
#include <omphalos/hwaddrs.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct l2host {
	unsigned char *hwaddr;
	struct l2host *next;
} l2host;

// FIXME support multiple zones, for different addrlens
static l2host *etherlist;

// FIXME replace internals with LRU acquisition...
static inline l2host *
create_l2host(const void *hwaddr,size_t addrlen){
	l2host *l2;

	printf("CREATE\n");
	if( (l2 = malloc(sizeof(*l2))) ){
		if((l2->hwaddr = malloc(addrlen)) == NULL){
			free(l2);
			return NULL;
		}
		memcpy(l2->hwaddr,hwaddr,addrlen);
		l2->next = etherlist;
		etherlist = l2;
	}
	return l2;
}

// FIXME strictly proof-of-concept. we'll want a trie- or hash-based
// lookup, backed by an arena-allocated LRU, etc...
l2host *lookup_l2host(const void *hwaddr,size_t addrlen){
	l2host *l2,**prev;

	if(addrlen != IFHWADDRLEN){
		fprintf(stderr,"Only 48-bit l2 addresses are supported\n");
		return NULL;
	}
	for(prev = &etherlist ; (l2 = *prev) ; prev = &l2->next){
		if(memcmp(l2->hwaddr,hwaddr,addrlen) == 0){
			*prev = l2->next;
			l2->next = etherlist;
			etherlist = l2;
			return l2;
		}
	}
	return create_l2host(hwaddr,addrlen);
}
