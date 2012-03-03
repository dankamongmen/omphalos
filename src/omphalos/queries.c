#include <net/if_arp.h>
#include <omphalos/icmp.h>
#include <omphalos/dhcp.h>
#include <omphalos/mdns.h>
#include <omphalos/lltd.h>
#include <omphalos/ssdp.h>
#include <omphalos/queries.h>
#include <omphalos/interface.h>

int query_network(int family,interface *i,const void *saddr){
	int r = 0;

	if(i->arptype != ARPHRD_LOOPBACK){
		r |= initiate_lltd(family,i,saddr);
		if(family == AF_INET){
			r |= tx_ipv4_bcast_pings(i,saddr);
			r |= dhcp4_probe(i,saddr);
		}else if(family == AF_INET6){
			r |= tx_ipv6_bcast_pings(i,saddr);
			r |= dhcp6_probe(i,saddr);
		}
		r |= mdns_sd_enumerate(family,i,saddr);
		r |= mdns_stdsd_probe(family,i,saddr);
		r |= ssdp_msearch(family,i,saddr);
	}
	return r;
}

