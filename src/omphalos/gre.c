#include <omphalos/gre.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define GRE_VERSION_NORMAL	0
#define GRE_VERSION_PPTP	1

// Followed by optional checksum, offset, key, seqnum, and source route entries
typedef struct grehdr {
	unsigned csumpres: 1;
	unsigned routingpres: 1;
	unsigned keypres: 1;
	unsigned seqpres: 1;
	unsigned ssrcpres: 1;
	unsigned recursectl: 3;
	unsigned flags: 5;
	unsigned version: 3;
	uint16_t protocol;
} __attribute__ ((packed)) grehdr;

void handle_gre_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct grehdr *gre = frame;
	unsigned glen;

	if(len < sizeof(*gre)){
		octx->diagnostic(L"%s malformed with %zu on %s",__func__,len,op->i->name);
		op->malformed = 1;
		return;
	}
	glen = 0;
	// If either the checksum or routing present bits are set, we require
	// a 16-bit checksum and a 16-bit offset.
	if(gre->csumpres || gre->routingpres){
		glen += 4;
	}
	// If the key present bit is set, require a 32-bit key.
	if(gre->keypres){
		glen += 4;
	}
	// If the sequence number present bit is set, require a 32-bit seqnum.
	if(gre->seqpres){
		glen += 4;
	}
	if(len < sizeof(*gre) + glen){
		octx->diagnostic(L"%s malformed with %zu on %s",__func__,len,op->i->name);
		op->malformed = 1;
		return;
	}
	if(gre->version != GRE_VERSION_NORMAL && gre->version != GRE_VERSION_PPTP){
		octx->diagnostic(L"%s noproto for %zu on %s",__func__,gre->version,op->i->name);
		op->malformed = 1;
		return;
	}
	// FIXME handle encapsulated protocol interpreting protocol as Ethernet
}
