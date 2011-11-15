#include <omphalos/diag.h>
#include <omphalos/firewire.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_firewire_packet(omphalos_packet *op,const void *frame,size_t len){
	diagnostic(L"FIXME firewire %p/%zu (%s)",frame,len,op->i->name);
}
