#include <sys/socket.h>
#include <linux/if.h>
#include <linux/irda.h>
#include <omphalos/irda.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_irda_packet(const omphalos_iface *octx,omphalos_packet *op,
					const void *frame,size_t len){
	const struct ethhdr *hdr = frame; // FIXME

	if(len < sizeof(*hdr)){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		return;
	}
}
