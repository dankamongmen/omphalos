#include <stdint.h>
#include <omphalos/ppp.h>
#include <omphalos/diag.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_ppp_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len);
	// FIXME handle...
}
