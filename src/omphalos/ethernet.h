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

struct omphalos_iface;
struct omphalos_packet;

void handle_ethernet_packet(const struct omphalos_iface *,
				struct omphalos_packet *,const void *,size_t);

// The actual length received might be off due to padding up to 60 octets,
// the minimum Ethernet frame (discounting 4-octet FCS). In the presence of
// 802.1q VLAN tagging, the minimum Ethernet frame is 64 bytes (again
// discounting the 4-octet FCS); LLC/SNAP encapsulation do not extend the
// minimum or maximum frame length. Allow such frames to go through formedness
// checks. Always use the uppermost protocol's measure of size!
static inline int
check_ethernet_padup(size_t rx,size_t expected){
	if(expected == rx){
		return 0;
	}else if(rx > expected){
		// 4 bytes for possible 802.1q VLAN tag. Probably ought verify
		// that 802.1q is actually in use FIXME.
		if(rx <= ETH_ZLEN + 4 - sizeof(struct ethhdr)){
			return 0;
		}
	}
	return 1;
}

// FIXME use a trie or bsearch
// FIXME generate data from a text file, preferably one taken from IANA or
// whoever administers the multicast address space
static inline const char *
name_ethmcastaddr(const void *mac){
	static const struct mcast {
		const unsigned char mac[ETH_ALEN];
		const char *name;
	} mcasts[] = {
		{	.mac = "\x01\x80\xc2\x00\x00\x00",
			.name = "Spanning Tree Protocol",
		},
	},*mc;

	for(mc = mcasts ; mc->name ; ++mc){
		if(memcmp(mac,mc->mac,ETH_ALEN) == 0){
			return mc->name;
		}
	}
	return NULL;
}

// Categorize an Ethernet address independent of context (this function never
// returns RTN_LOCAL or RTN_BROADCAST, for instance).
static inline int
categorize_ethaddr(const void *mac){
	if(((const unsigned char *)mac)[0] & 0x1){
		return RTN_MULTICAST;
	}
	return RTN_UNICAST;
}

#ifdef __cplusplus
}
#endif

#endif
