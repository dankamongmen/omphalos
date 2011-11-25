#include <sys/types.h>
#include <linux/tcp.h>
#include <omphalos/tcp.h>
#include <omphalos/dns.h>
#include <omphalos/diag.h>
#include <omphalos/mdns.h>
#include <asm/byteorder.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define DNS_TCP_PORT 53
#define MDNS_TC_PORT 5353

void handle_tcp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct tcphdr *tcp = frame;

	if(len < sizeof(*tcp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	op->l4src = tcp->source;
	op->l4dst = tcp->dest;
	if(len < tcp->doff){
		diagnostic("%s options malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME need reassemble the TCP stream before analyzing it...
}
