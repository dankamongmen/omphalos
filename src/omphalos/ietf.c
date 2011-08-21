#include <assert.h>
#include <string.h>
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
#define RFC1112_LOCAL_MDNS		__constant_htonl(0xe00000fb)

#include <stdio.h>
static const char *
ietf_multicast_ipv4(const uint32_t *ip){
	if((*ip & RFC1112_MASK) != RFC1112_BASE){
		return NULL;
	}
	if((*ip & RFC1112_LOCAL_MASK) != RFC1112_LOCAL_BASE){
		return "RFC 1112 multicast";
	}
	if(*ip == RFC1112_LOCAL_ALL_HOSTS){
		return "All segment hosts (RFC 1112)";
	}else if(*ip == RFC1112_LOCAL_ALL_ROUTERS){
		return "All segment routers (RFC 1112)";
	}else if(*ip == RFC1112_LOCAL_MDNS){
		return "mDNS (IANA)";
	}
	return "RFC 1112 segment multicast";
}

static const struct {
	uint128_t ip;
	const char *name;
} ip6mcasts[] = {
	{ // ff02::1
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000001), },
		.name = "All segment hosts (RFC 2375)",
	},
	{ // ff02::2
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000002), },
		.name = "All segment routers (RFC 2375)",
	},
	{ // ff02::fb
		.ip = { __constant_htonl(0xff020000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x000000fb), },
		.name = "mDNS (IANA)",
	},
	{ // ff05::2
		.ip = { __constant_htonl(0xff050000), __constant_htonl(0x00000000),
			__constant_htonl(0x00000000), __constant_htonl(0x00000002), },
		.name = "All site routers (RFC 2375)",
	},
	{ .name = NULL, }
};

static const char *
ietf_multicast_ipv6(const uint32_t *ip){
	uint128_t alignedip = { ip[0], ip[1], ip[2], ip[3] };
	const typeof(*ip6mcasts) *mcast;

	if(!IN6_IS_ADDR_MULTICAST(&alignedip)){
		return NULL;
	}
	for(mcast = ip6mcasts ; (*mcast).name ; ++mcast){
		if(equal128(alignedip,(*mcast).ip)){
			return (*mcast).name;
		}
	}
	return "RFC 4291 multicast";
}

const char *ietf_multicast_lookup(int fam,const void *addr){
	if(fam == AF_INET){
		return ietf_multicast_ipv4(addr);
	}else if(fam == AF_INET6){
		return ietf_multicast_ipv6(addr);
	}
	return NULL;
}
