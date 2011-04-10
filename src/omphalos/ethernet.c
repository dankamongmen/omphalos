#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

void handle_ethernet_packet(interface *iface,const void *frame,size_t len __attribute__ ((unused))){
	const struct ethhdr *hdr = frame;
	struct l2host *l2;

	++iface->pkts;
	if( (l2 = lookup_l2host(hdr->h_source,ETH_ALEN)) ){
		frame = NULL; // FIXME
	}
}

