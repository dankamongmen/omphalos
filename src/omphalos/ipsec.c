#include <stdint.h>
#include <linux/ip.h>
#include <omphalos/diag.h>
#include <omphalos/ipsec.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Encapsulated Security Payload
void handle_esp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct ip_esp_hdr *esp = frame;
	if(len < sizeof(*esp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

// Authentication Header
void handle_ah_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct ip_auth_hdr *ah = frame;
	if(len < sizeof(*ah)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
}
