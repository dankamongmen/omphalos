#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <omphalos/128.h>
#include <asm/byteorder.h>
#include <omphalos/ietf.h>

#define RFC1112_BASE __constant_htonl(0xe0000000)
#define RFC1112_MASK __constant_htonl(0xe0000000)

#define RFC1112_LOCAL_BASE __constant_htonl(0xe0000000)
#define RFC1112_LOCAL_MASK __constant_htonl(0xffffff00)

#define RFC1112_LOCAL_ALL_HOSTS		__constant_htonl(0xe0000001)
#define RFC1112_LOCAL_ALL_ROUTERS	__constant_htonl(0xe0000002)

#include <stdio.h>
static const char *
ietf_multicast_ipv4(const uint32_t *ip){
	if((*ip & RFC1112_MASK) != RFC1112_BASE){
		return NULL;
	}
	if((*ip & RFC1112_LOCAL_MASK) != RFC1112_LOCAL_BASE){
		return "RFC 1112 IPv4 multicast";
	}
	if(*ip == RFC1112_LOCAL_ALL_HOSTS){
		return "RFC 1112 all segment hosts";
	}else if(*ip == RFC1112_LOCAL_ALL_ROUTERS){
		return "RFC 1112 all segment routers";
	}
	return "RFC 1112 IPv4 segment multicast";
}

static const char *
ietf_multicast_ipv6(const uint128_t *ip){
	if(IN6_IS_ADDR_MULTICAST(ip)){
		return "RFC 4291 IPv6 multicast";
	}
	return NULL;
}

const char *ietf_multicast_lookup(int fam,const void *addr){
	if(fam == AF_INET){
		return ietf_multicast_ipv4(addr);
	}else if(fam == AF_INET6){
		return ietf_multicast_ipv6(addr);
	}
	return NULL;
}
