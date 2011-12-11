#include <omphalos/diag.h>
#include <omphalos/lltd.h>
#include <omphalos/service.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct lltdhdr {
	uint8_t version;
	uint8_t tos;
	uint8_t reserved;
	uint8_t function;
} __attribute__ ((packed)) lltdhdr;

void handle_lltd_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct lltdhdr *lltd = frame;

	if(len < sizeof(*lltd)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
}
