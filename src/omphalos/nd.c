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

// Take as input everything following the ICMPv6 header
void handle_nd_routersol(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len){
		diagnostic(L"%s trailing data (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
	}
}

void handle_nd_neighsol(struct omphalos_packet *op,const void *frame __attribute__ ((unused)),size_t len){
	const interface *i = op->i;

	if(len){
		diagnostic(L"%s trailing data (%zu) on %s",__func__,len,i->name);
		op->malformed = 1;
		return;
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
