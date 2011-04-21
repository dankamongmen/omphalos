#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <omphalos/netaddrs.h>
#include <omphalos/interface.h>

// No need to store addrlen, since all objects in a given arena have the
// same length of hardware address.
typedef struct iphost {
	uint32_t ip;		// network byte order
	struct iphost *next;
} iphost;

typedef struct ipv6host {
	struct in6_addr ip;	// network byte order
	struct ipv6host *next;
} ipv6host;

// FIXME linked list is grotesque
static iphost *iplist;
static ipv6host *ipv6list;

// FIXME replace internals with LRU acquisition...
static inline iphost *
create_iphost(const interface *iface,const uint32_t ip){
	iphost *i;

	if(is_local4(iface,ip)){
		return NULL;
	}
	if( (i = malloc(sizeof(*i))) ){
		i->ip = ip;
		i->next = iplist;
		iplist = i;
	}
	return i;
}

// FIXME strictly proof-of-concept. we'll want a trie- or hash-based
// lookup, backed by an arena-allocated LRU, etc...
iphost *lookup_iphost(const interface *iface,const void *addr){
	iphost *ip,**prev;
	uint32_t i;

	memcpy(&i,addr,sizeof(i)); // input might not be word-aligned
	for(prev = &iplist ; (ip = *prev) ; prev = &ip->next){
		if(ip->ip == i){
			*prev = ip->next;
			ip->next = iplist;
			iplist = ip;
			return ip;
		}
	}
	return create_iphost(iface,i);
}

void cleanup_l3hosts(void){
	ipv6host *ip6,*tmp6;
	iphost *ip,*tmp;

	for(ip = iplist ; ip ; ip = tmp){
		tmp = ip->next;
		free(ip);
	}
	for(ip6 = ipv6list ; ip6 ; ip6 = tmp6){
		tmp6 = ip6->next;
		free(ip6);
	}
	ipv6list = NULL;
	iplist = NULL;
}

// FIXME
#define INET6_ADDRSTRLEN 46
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
// end FIXME

int print_l3hosts(FILE *fp){
	if(iplist || ipv6list){
		char str[INET6_ADDRSTRLEN];
		const ipv6host *i6;
		const iphost *i;

		if(fprintf(fp,"<hosts>") < 0){
			return -1;
		}
		for(i = iplist ; i ; i = i->next){
			inet_ntop(AF_INET,&i->ip,str,sizeof(str));
			if(fprintf(fp,"<ipv4 addr=\"%s\"/>",str) < 0){
				return -1;
			}
		}
		for(i6 = ipv6list ; i6 ; i6 = i6->next){
			inet_ntop(AF_INET6,&i6->ip,str,sizeof(str));
			if(fprintf(fp,"<ipv6 addr=\"%s\"/>",str) < 0){
				return -1;
			}
		}
		if(fprintf(fp,"</hosts>") < 0){
			return -1;
		}
	}
	return 0;
}
