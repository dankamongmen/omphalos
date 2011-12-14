#ifndef OMPHALOS_ETHERNET
#define OMPHALOS_ETHERNET

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <sys/socket.h>
#include <linux/if_ether.h>
#include <linux/rtnetlink.h>
#include <omphalos/interface.h>

struct omphalos_packet;

// STP actually travels over 802.3 with SAP == 0x42 (most of the time).
#define ETH_P_STP	0x0802
#define ETH_P_LLDP	0x88cc
#define ETH_P_CTP	0x9000
// FIXME for SAP/DAP == 0xfe in 802.3 traffic ("routed osi pdu's")
#define ETH_P_OSI	ETH_P_802_3

void handle_ethernet_packet(struct omphalos_packet *,const void *,size_t);

int prep_eth_header(void *,size_t,const struct interface *,const void *,
			uint16_t) __attribute__ ((nonnull (1,3,4)));

static inline int
prep_eth_bcast(void *frame,size_t len,const interface *i,uint16_t proto){
	return prep_eth_header(frame,len,i,i->bcast,proto);
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

// Input: 48-bit MAC address as 6 octets
static inline uint64_t
eui64(const void *addr){
	const uint64_t EUI64 = 0x020000fffe000000ull;
	uint64_t front,back;

	memcpy(&front,addr,3);		// FIXME borked on big-endian
	memcpy(&back,addr + 3,3);
	front <<= 5;
	return front | EUI64 | back;
}

#ifdef __cplusplus
}
#endif

#endif
