#include <omphalos/stp.h>
#include <omphalos/diag.h>
#include <linux/if_ether.h>
#include <omphalos/service.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

struct bridgeid {
	uint16_t prio;
	unsigned char mac[ETH_ALEN];
} __attribute__ ((packed));

struct bdpu {
	uint16_t protocol;
	uint8_t version;
	uint8_t bdputype;
	uint8_t flags;
	struct bridgeid root;
	uint32_t rootcost;
	struct bridgeid sender;
	uint16_t sendport;
	uint16_t age;
	uint16_t maxage;
	uint16_t hellotime;
	uint16_t fwddelay;
} __attribute__ ((packed));

void handle_stp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct bdpu *bdpu = frame;
	struct l2host *l2s,*l2b;

	if(len < sizeof(*bdpu)){
		diagnostic("%s packet too small (%zu < %zu) on %s",__func__,
				len,sizeof(*bdpu),op->i->name);
		op->malformed = 1;
		return;
	}
	if(bdpu->protocol){
		diagnostic("%s Unknown STP proto (%hu) on %s",__func__,bdpu->protocol,op->i->name);
		op->noproto = 1;
		return;
	}
	if(bdpu->version){
		diagnostic("%s Unknown STP version (%u) on %s",__func__,bdpu->version,op->i->name);
		op->noproto = 1;
		return;
	}
	l2b = lookup_l2host(op->i,bdpu->root.mac);
	observe_proto(op->i,l2b,L"Root bridge");
	l2s = lookup_l2host(op->i,bdpu->sender.mac);
	observe_proto(op->i,l2s,L"Bridge");
	// FIXME
}
