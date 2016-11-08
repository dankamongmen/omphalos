#include <omphalos/diag.h>
#include <omphalos/ospf.h>
#include <omphalos/omphalos.h>

typedef struct ospfhdr {
	uint8_t version;
	uint8_t type;
	uint16_t length;
	uint32_t router_id;
	uint32_t area_id;
	uint16_t csum;
	uint16_t auth_type;
	uint64_t auth;
} __attribute__ ((packed)) ospfhdr;

void handle_ospf_packet(omphalos_packet *op,const void *frame,size_t len){
	const ospfhdr *ospf = frame;

	if(len < sizeof(*ospf)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}
