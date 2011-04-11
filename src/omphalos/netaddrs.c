#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <omphalos/netaddrs.h>

struct ipv6host;

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct iphost {
	uint32_t ip;		// host byte order
	struct iphost *next;
} iphost;

// FIXME support multiple zones, for different addrlens
static iphost *iplist;

// FIXME replace internals with LRU acquisition...
static inline iphost *
create_iphost(const void *hwaddr,size_t addrlen){
	iphost *l2;

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
iphost *lookup_iphost(const void *hwaddr,size_t addrlen){
	iphost *l2,**prev;

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
	return create_iphost(hwaddr,addrlen);
}

void cleanup_l3hosts(void){
	iphost *l3,*tmp;

	for(l3 = iplist ; l3 ; l3 = tmp){
		tmp = l3->next;
		free(l3);
	}
	iplist = NULL;
}

char *ipaddrstr(const void *addr,size_t len){
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

int print_l3hosts(FILE *fp){
	const iphost *i;

	if( (i = iplist) ){
		if(fprintf(fp,"<hosts>") < 0){
			return -1;
		}
		do{
			char *l3addr = ipaddrstr(i->addr);

			if(fprintf(fp,"<ipv4 addr=\"%s\"/>",l3addr) < 0){
				return -1;
			}
			free(l3addr);
		}while( (i = i->next) );
		if(fprintf(fp,"</hosts>") < 0){
			return -1;
		}
	}
	return 0;
}
