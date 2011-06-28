#include <omphalos/firewire.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_firewire_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	octx->diagnostic("FIXME firewire %p/%zu (%s)\n",frame,len,op->i->name);
	if(octx->packet_read){
		octx->packet_read(op->i->opaque,op);
	}
}
