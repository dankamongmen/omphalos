#include <assert.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <omphalos/omphalos.h>

void handle_mdns_packet(const omphalos_iface *iface,omphalos_packet *op,
			const void *frame,size_t len){
	handle_dns_packet(iface,op,frame,len);
}
