#include <omphalos/icmp.h>
#include <omphalos/mdns.h>
#include <omphalos/lltd.h>
#include <omphalos/ssdp.h>
#include <omphalos/queries.h>
#include <omphalos/interface.h>

int query_network(int family,interface *i,const void *saddr){
	int r = 0;

	r |= initiate_lltd(family,i,saddr);
	r |= tx_broadcast_pings(family,i,saddr);
	r |= mdns_sd_enumerate(family,i,saddr);
	r |= mdns_stdsd_probe(family,i,saddr);
	r |= ssdp_msearch(family,i,saddr);
	return r;
}

