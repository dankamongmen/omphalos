#include <linux/icmp.h>
#include <omphalos/tx.h>
#include <omphalos/icmp.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_icmp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

void handle_icmp6_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
static int
tx_ipv4_bcast_pings(interface *i){
	assert(i); // FIXME
	return -1;
}

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
static int
tx_ipv6_bcast_pings(interface *i){
	const struct ip6route *i6;
	int ret = 0;

	for(i6 = i->ip6r ; i6 ; i6 = i6->next){
		size_t flen;
		void *frame;

		if((frame = get_tx_frame(i,&flen)) == NULL){
			ret = -1;
			continue;
		}
		// FIXME prepare ipv6 broadcast
		send_tx_frame(i,frame); // FIXME get return value...
	}
	return ret;
}

int tx_broadcast_pings(int fam,interface *i){
	if(fam == AF_INET){
		return tx_ipv4_bcast_pings(i);
	}else if(fam == AF_INET6){
		return tx_ipv6_bcast_pings(i);
	}
	return -1;
}
