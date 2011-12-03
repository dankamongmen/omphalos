#include <stdint.h>
#include <omphalos/diag.h>
#include <omphalos/irda.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct hdlchdr {
	uint8_t address;
	uint8_t control;
	uint16_t proto;
} hdlchdr;

void handle_hdlc_packet(omphalos_packet *op,const void *frame,size_t len){
	const hdlchdr *hdr = frame;

	if(len < sizeof(*hdr)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	// FIXME handle...
}
