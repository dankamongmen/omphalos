#include <assert.h>
#include <omphalos/mdns.h>
#include <omphalos/omphalos.h>

void handle_mdns_packet(const omphalos_iface *iface,omphalos_packet *op,
			const void *frame,size_t len){
	assert(iface && op && frame && len); // FIXME
}
