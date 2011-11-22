#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <omphalos/nd.h>
#include <netinet/icmp6.h>
#include <asm/byteorder.h>
#include <omphalos/diag.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

static const unsigned char PROBESRC[16] = {};

// Takes as input everything following the IPv6 header
void handle_nd_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmp6_hdr *icmp;
	const interface *i = op->i;

	icmp = frame;
	if(len < sizeof(*icmp)){
		diagnostic(L"%s too short (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
	// FIXME this switch will need be moved into ICMPv6 handling, and this
	// function broken up into several...
	switch(icmp->icmp6_type){
		case ND_ROUTER_ADVERT:
			break;
		case ND_NEIGHBOR_ADVERT:
			break;
		case ND_ROUTER_SOLICIT:
			break;
		case ND_NEIGHBOR_SOLICIT:
			break;
		case ND_REDIRECT:
			break;
		default:
			diagnostic(L"Not a ND message type: %u",icmp->icmp6_type);
			break;
	}
}
