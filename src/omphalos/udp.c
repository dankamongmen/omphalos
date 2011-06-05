#include <sys/types.h>
#include <linux/udp.h>
#include <omphalos/udp.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_udp_packet(const omphalos_iface *octx,interface *i,const void *frame,size_t len){
	const struct udphdr *udp = frame;

	if(len < sizeof(*udp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME
}

