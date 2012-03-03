#include <assert.h>
#include <omphalos/dhcp.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

int handle_dhcp_packet(omphalos_packet *op,const void *frame,size_t flen){
	assert(op && frame && flen);
	return 1; // FIXME
}

int handle_dhcp6_packet(omphalos_packet *op,const void *frame,size_t flen){
	assert(op && frame && flen);
	return 1; // FIXME
}

int dhcp4_probe(interface *i,const uint32_t *saddr){
	assert(i && saddr);
	return 1; // FIXME
}

int dhcp6_probe(interface *i,const uint128_t saddr){
	assert(i && saddr);
	return 1; // FIXME
}
