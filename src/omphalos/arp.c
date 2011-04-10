#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/arp.h>
#include <omphalos/interface.h>

void handle_arp_packet(interface *i,const void *frame,size_t len){
	const struct arphdr *ap = frame;

	if(len < sizeof(*ap)){
		++i->malformed;
		return;
	}
	// FIXME...
}
