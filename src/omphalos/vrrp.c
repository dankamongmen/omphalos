#include <stdint.h>
#include <omphalos/vrrp.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct vrrphdr {
	struct {
		unsigned version: 4;
		unsigned type: 4;
	};
	uint8_t vrtrid;
	uint8_t prio;
	uint8_t addrcnt;
	uint8_t authtype;
	uint8_t adver;
	uint16_t csum;
	// addresses follow
} vrrphdr;

void handle_vrrp_packet(omphalos_packet *op,const void *frame,size_t len){
	const vrrphdr *vrrp = frame;

	if(len < sizeof(*vrrp)){
		diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}
