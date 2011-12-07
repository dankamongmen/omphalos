#ifndef OMPHALOS_IETF
#define OMPHALOS_IETF

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <omphalos/128.h>

const wchar_t *ietf_multicast_lookup(int,const void *);
const wchar_t *ietf_bcast_lookup(int,const void *);
const wchar_t *ietf_local_lookup(int,const void *);

// We take the IP in network byte order
static inline int
unrouted_ip4(const uint32_t *ip){
	const uint32_t LINKLOCAL4_MASK = 0xffff0000ul;
	const uint32_t LINKLOCAL4_NET = 0xa9fe0000ul;
	const uint32_t LOOPBACK4_MASK = 0xff000000ul;
	const uint32_t LOOPBACK4_NET = 0x7f000000ul;

	if((ntohl(*ip) & LINKLOCAL4_MASK) == LINKLOCAL4_NET){
		return 1;
	}
	if((ntohl(*ip) & LOOPBACK4_MASK) == LOOPBACK4_NET){
		return 1;
	}
	return 0;
}

static inline int
unrouted_ip6(const uint128_t ip6){
	const uint128_t LINKLOCAL6_MASK = { htonl(0xffb00000ul), 0, 0, 0 };
	const uint128_t LINKLOCAL6_NET = { htonl(0xfe800000ul), 0, 0, 0 };
	uint128_t masked;

	memcpy(masked,ip6,sizeof(masked));
	andequals128(masked,LINKLOCAL6_MASK);
	return equal128(masked,LINKLOCAL6_NET);
}

#ifdef __cplusplus
}
#endif

#endif
