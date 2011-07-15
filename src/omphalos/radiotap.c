#include <sys/socket.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME i guess we need a copy of the upstream, since linux doesn't
// seem to install it? dubious, very dubious...
typedef struct radiotaphdr {
	char data[0x18];
} __attribute__ ((packed)) radiotaphdr;

void handle_radiotap_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const char *sframe;

	if(len < sizeof(radiotaphdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	sframe = (const char *)frame + sizeof(radiotaphdr);
	len -= sizeof(radiotaphdr);
	handle_ethernet_packet(octx,op,sframe,len);
}
