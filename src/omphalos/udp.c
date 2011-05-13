#include <sys/types.h>
#include <linux/udp.h>
#include <omphalos/udp.h>
#include <omphalos/interface.h>

void handle_udp_packet(interface *i,const void *frame,size_t len){
	const struct udphdr *udp = frame;

	if(len < sizeof(*udp)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME
}

