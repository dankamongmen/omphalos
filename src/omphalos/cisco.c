#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <omphalos/diag.h>
#include <asm/byteorder.h>
#include <omphalos/cisco.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct udldattr {
	uint16_t type;
	uint16_t len;
} __attribute__ ((packed)) udldattr;

typedef struct udldhdr {
	struct {
		unsigned version: 3;
		unsigned opcode: 5;
	} __attribute__ ((packed));
	uint8_t flags;
	uint16_t csum;
	// PDUs follow
} __attribute__ ((packed)) udldhdr;

#define UDLD_TYPE_DEVID __constant_htons(0x1)

void handle_udld_packet(omphalos_packet *op,const void *frame,size_t len){
	const udldhdr *udld = frame;
	const udldattr *ua;

	if(len < sizeof(*udld)){
		op->malformed = 1;
		diagnostic("%s packet too small (%zu) on %s",__func__,len,op->i->name);
		return;
	}
	ua = (const udldattr *)((const char *)frame + sizeof(*udld));
	len -= sizeof(*udld);
	while(len){
		if(len < sizeof(*ua)){
			op->malformed = 1;
			diagnostic("%s attr too small (%zu) on %s",__func__,len,op->i->name);
			return;
		}
		if(len < ntohs(ua->len)){
			op->malformed = 1;
			diagnostic("%s attr too large (%hu) on %s",__func__,ntohs(ua->len),op->i->name);
			return;
		}
		// FIXME it'd be nice to use this name for some purpose
		/*switch(ua->type){
			case UDLD_TYPE_DEVID:{
				int devlen = ntohs(ua->len) - 4;
				const char *name = (const char *)ua + 4;

				diagnostic("UDLD device name: %*s",devlen,name);
				break;
			}
		}*/
		len -= ntohs(ua->len);
		ua = (const udldattr *)((const char *)ua + ntohs(ua->len));
	}
}

void handle_cld_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len); // FIXME
}

typedef struct eigrphdr {
	uint8_t version;
	uint8_t opcode;
	uint16_t csum;
	uint32_t flags, seq, ack, asn;
	// TLVs follow
} __attribute__ ((packed)) eigrphdr;

void handle_eigrp_packet(omphalos_packet *op,const void *frame,size_t len){
	const eigrphdr *eigrp = frame;

	if(len < sizeof(*eigrp)){
		op->malformed = 1;
		diagnostic("%s packet too small (%zu) on %s",__func__,len,op->i->name);
		return;
	}
	// FIXME
}

void handle_dtp_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len); // FIXME
}
