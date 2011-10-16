#include <omphalos/firewire.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_firewire_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	octx->diagnostic(L"FIXME firewire %p/%zu (%s)",frame,len,op->i->name);
}
