#include <stdint.h>
#include <omphalos/dns.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

struct dnshdr {
	uint16_t id;
	uint16_t crap;
	uint16_t qdcount,ancount,nscount,arcount;
	// question, answer, authority, and additional sections follow
};

void handle_dns_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct dnshdr *dns = frame;
	uint16_t qd,an,ns,ar;

	if(len < sizeof(*dns)){
		octx->diagnostic("%s malformed with %zu on %s",
				__func__,len,op->i->name);
		++op->i->malformed;
		return;
	}
	qd = ntohs(dns->qdcount);
	an = ntohs(dns->ancount);
	ns = ntohs(dns->nscount);
	ar = ntohs(dns->arcount);
	octx->diagnostic("ID: %hu Q/A/N/A: %hu/%hu/%hu/%hu",dns->id,qd,an,ns,ar);
}
