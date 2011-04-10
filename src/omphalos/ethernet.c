#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

void handle_ethernet_packet(interface *iface,const struct timeval *tv __attribute__ ((unused)),
		const void *frame,size_t len __attribute__ ((unused)),const unsigned char *hwaddr){
	struct l2host *l2;

	++iface->pkts;
	if( (l2 = lookup_l2host(hwaddr,ETH_ALEN)) ){
		frame = NULL; // FIXME
	}
}

