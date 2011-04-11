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

	if( (l2 = malloc(sizeof(*l2))) ){
		char *hwstr;

		if((l2->hwaddr = malloc(addrlen)) == NULL){
			free(l2);
			return NULL;
		}
		if( (hwstr = l2addrstr(hwaddr,addrlen)) ){ // FIXME
			printf("New neighbor: %s\n",hwstr);
			free(hwstr);
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

void cleanup_l2hosts(void){
	l2host *l2,*tmp;

	for(l2 = etherlist ; l2 ; l2 = tmp){
		tmp = l2->next;
		free(l2->hwaddr);
		free(l2);
	}
	etherlist = NULL;
}

char *l2addrstr(const void *addr,size_t len){
	unsigned idx;
	size_t s;
	char *r;

	// Each byte becomes two ASCII characters and either a separator or a nul
	s = len * 3;
	if( (r = malloc(s)) ){
		for(idx = 0 ; idx < len ; ++idx){
			snprintf(r + idx * 3,s - idx * 3,"%02x:",((unsigned char *)addr)[idx]);
		}
	}
	return r;
}

int print_l2hosts(FILE *fp){
	const l2host *l2;

	if( (l2 = etherlist) ){
		if(fprintf(fp,"<neighbors>") < 0){
			return -1;
		}
		do{
			char *hwaddr = l2addrstr(l2->hwaddr,IFHWADDRLEN);

			if(fprintf(fp,"<ieee802 addr=\"%s\"/>",hwaddr) < 0){
				return -1;
			}
			free(hwaddr);
		}while( (l2 = l2->next) );
		if(fprintf(fp,"</neighbors>") < 0){
			return -1;
		}
	}
	return 0;
}
