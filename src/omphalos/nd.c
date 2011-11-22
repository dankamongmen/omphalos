#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <omphalos/nd.h>
#include <netinet/icmp6.h>
#include <asm/byteorder.h>
#include <omphalos/diag.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

struct icmp6_op {
	uint8_t type;
	uint8_t len;
};

#define ICMP6_OP_SRCLINK 1

// Take as input everything following the ICMPv6 header
void handle_nd_routersol(struct omphalos_packet *op,const void *frame,size_t len){
	const interface *i = op->i;

	if(len < 4){ // First four bytes MUST be ignored by receiver
		diagnostic(L"%s data too small (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
	len -= 4;
	frame = (const char *)frame + 4;
	while(len){
		const struct icmp6_op *iop = frame;

		if(len < 2){
			diagnostic(L"%s op too small (%zu) on %s",__func__,len,i->name);
			op->malformed = 1;
			return;
		}
		if(len < iop->len * 8){
			diagnostic(L"%s opdata too small (%zu) on %s",__func__,len,i->name);
			op->malformed = 1;
			return;
		}
		switch(iop->type){
			case ICMP6_OP_SRCLINK:
				// FIXME do something?
				break;
			default:
				diagnostic(L"%s unknown option (%u)",__func__,iop->type);
				op->noproto = 1;
				return;
		}
		if(iop->len < 1){
			diagnostic(L"%s bogon oplen (%u)",__func__,iop->len);
			op->malformed = 1;
			assert(0);
			return;
		}
		len -= iop->len * 8;
		frame = (const char *)frame + iop->len * 8;
	}
}

void handle_nd_neighsol(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len < 4){ // First four bytes MUST be ignored by receiver
		diagnostic(L"%s data too small (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
	len -= 4;
	frame = (const char *)frame + 4;
	while(len){
		const struct icmp6_op *iop = frame;

		if(len < 2){
			diagnostic(L"%s op too small (%zu) on %s",__func__,len,i->name);
			op->malformed = 1;
			return;
		}
		if(len < iop->len * 8){
			diagnostic(L"%s opdata too small (%zu) on %s",__func__,len,i->name);
			op->malformed = 1;
			return;
		}
		switch(iop->type){
			case ICMP6_OP_SRCLINK:
				// FIXME do something?
				break;
			default:
				diagnostic(L"%s unknown option (%u)",__func__,iop->type);
				op->noproto = 1;
				return;
		}
		if(iop->len < 1){
			diagnostic(L"%s bogon oplen (%u)",__func__,iop->len);
			op->malformed = 1;
			assert(0);
			return;
		}
		len -= iop->len * 8;
		frame = (const char *)frame + iop->len * 8;
	}
}

void handle_nd_routerad(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len){
		diagnostic(L"%s trailing data (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
}

void handle_nd_neighad(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len){
		diagnostic(L"%s trailing data (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
}

void handle_nd_redirect(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len){
		diagnostic(L"%s trailing data (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
}