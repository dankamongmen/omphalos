#include <linux/ip.h>
#include <sys/types.h>
#include <netinet/ip6.h>
#include <omphalos/diag.h>
#include <netinet/icmp6.h>
#include <asm/byteorder.h>
#include <omphalos/service.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME we want an automata-based approach to generically match. until then,
// use well-known ports, ugh...
void handle_mld_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct mld_hdr *mld = frame;

	if(len < sizeof(*mld)){
		diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
}
