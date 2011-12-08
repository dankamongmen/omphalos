#include <assert.h>
#include <omphalos/dhcp.h>
#include <omphalos/omphalos.h>

int handle_dhcp_packet(omphalos_packet *op,const void *frame,size_t flen){
	assert(op && frame && flen);
	return 1; // FIXME
}

int handle_dhcp6_packet(omphalos_packet *op,const void *frame,size_t flen){
	assert(op && frame && flen);
	return 1; // FIXME
}
