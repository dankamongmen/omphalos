#include <omphalos/mdns.h>
#include <omphalos/icmp.h>
#include <omphalos/queries.h>
#include <omphalos/interface.h>

int query_network(int family,interface *i,const void *saddr){
	int r = 0;

	r |= tx_broadcast_pings(family,i,saddr);
	r |= mdns_sd_enumerate(family,i,saddr);
	r |= mdns_stdsd_probe(family,i,saddr);
	return r;
}

