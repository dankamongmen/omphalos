#include <assert.h>
#include <omphalos/dhcp.h>
#include <omphalos/omphalos.h>

void handle_dhcp_packet(const omphalos_iface *octx,omphalos_packet *op,
					const void *frame,size_t flen){
	assert(octx && op && frame && flen);
}
