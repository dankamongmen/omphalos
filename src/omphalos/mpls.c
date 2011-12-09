#include <stdint.h>
#include <omphalos/diag.h>
#include <omphalos/mpls.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// MultiProtocol Label Switching
typedef struct mplshdr {
	unsigned label: 20;
	unsigned class: 3;
	unsigned bottom: 1;
	unsigned ttl: 8;
} __attribute__ ((packed)) mplshdr;

void handle_mpls_packet(omphalos_packet *op,const void *frame,size_t len){
	const mplshdr *mpls = frame;

	if(len < sizeof(*mpls)){
		op->malformed = 1;
		diagnostic("%s packet too small (%zu) on %s",__func__,len,op->i->name);
		return;
	}
	// FIXME how do we know what lives underneath?
}
