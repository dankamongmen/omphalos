#ifndef OMPHALOS_ETHERNET
#define OMPHALOS_ETHERNET

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stddef.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/rtnetlink.h>

struct interface;
struct omphalos_iface;
struct omphalos_packet;

void handle_ethernet_packet(const struct omphalos_iface *,struct interface *,
				struct omphalos_packet *,const void *,size_t);

// The actual length received might be off due to padding up to 60 octets,
// the minimum Ethernet frame (discounting 4-octet FCS). Allow such frames
// to go through formedness checks. Always use the uppermost protocol's
// measure of size!
static inline int
check_ethernet_padup(size_t rx,size_t expected){
	if(expected == rx){
		return 0;
	}else if(rx > expected){
		if(rx <= ETH_ZLEN - sizeof(struct ethhdr)){
			return 0;
		}
	}
	return 1;
}

static inline int
categorize_ethaddr(const void *mac){
	static const unsigned char brd[] = "\xff\xff\xff\xff\xff\xff";

	if(((const unsigned char *)mac)[0] & 0x1){
		// Can't use sizeof(brd), since it has a terminating NUL :/
		if(memcmp(mac,brd,IFHWADDRLEN) == 0){
			return RTN_BROADCAST;
		}
		return RTN_MULTICAST;
	}
	return RTN_UNICAST;
}

#ifdef __cplusplus
}
#endif

#endif
