#include <omphalos/diag.h>
#include <omphalos/sctp.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct sctphdr {
	uint16_t src,dst;
	uint32_t verification;
	uint32_t checksum;
} __attribute__ ((packed)) sctphdr;

void handle_sctp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct sctphdr *sctp = frame;

	if(len < sizeof(*sctp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	op->l4src = sctp->src;
	op->l4dst = sctp->dst;
	// FIXME
}
