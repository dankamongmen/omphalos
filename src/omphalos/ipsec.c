#include <stdint.h>
#include <omphalos/diag.h>
#include <omphalos/ipsec.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Encapsulated Security Payload
void handle_esp_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len); // FIXME
}

// Authentication Header
void handle_ah_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len); // FIXME
}
