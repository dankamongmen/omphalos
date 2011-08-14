#include <linux/icmp.h>
#include <omphalos/icmp.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_icmp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}

void handle_icmp6_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}
