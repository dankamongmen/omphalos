#include <stdint.h>
#include <omphalos/pim.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct pimhdr {
	unsigned pimver: 4;
	unsigned pimtype: 4;
	unsigned reserved: 8;
	uint16_t csum;
} pimhdr;

void handle_pim_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct pimhdr *pim = frame;

	if(len < sizeof(*pim)){
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}
