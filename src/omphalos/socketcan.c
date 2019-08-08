#include <omphalos/diag.h>
#include <omphalos/omphalos.h>
#include <omphalos/socketcan.h>
#include <omphalos/interface.h>

void handle_can_packet(omphalos_packet *op, const void *frame, size_t len){
	diagnostic("FIXME firewire %p/%zu (%s)", frame, len, op->i->name);
}
