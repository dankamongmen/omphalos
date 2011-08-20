#include <sys/types.h>
#include <linux/tcp.h>
#include <omphalos/tcp.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <asm/byteorder.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define DNS_TCP_PORT 53
#define MDNS_TC_PORT 5353

void handle_tcp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct tcphdr *tcp = frame;

	if(len < sizeof(*tcp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME check header len etc...
}
