#include <sys/socket.h>
#include <linux/ipx.h>
#include <omphalos/ipx.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Taken from the linux kernel's include/net/ipx.h
struct ipx_address {
	__be32  net;
	__u8    node[IPX_NODE_LEN]; 
	__be16  sock;
};

#define IPX_TYPE_UNKNOWN	0x00
#define IPX_TYPE_RIP		0x01	/* may also be 0 */
#define IPX_TYPE_SAP		0x04	/* may also be 0 */
#define IPX_TYPE_SPX		0x05	/* SPX protocol */
#define IPX_TYPE_NCP		0x11	/* $lots for docs on this (SPIT) */
#define IPX_TYPE_PPROP		0x14	/* complicated flood fill brdcast */

struct ipxhdr {
	__be16			ipx_checksum __attribute__ ((packed));
	__be16			ipx_pktsize __attribute__ ((packed));
	__u8			ipx_tctrl;
	__u8			ipx_type;
	struct ipx_address	ipx_dest __attribute__ ((packed));
	struct ipx_address	ipx_source __attribute__ ((packed));
};

void handle_ipx_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct ipxhdr *ipxhdr = frame;
	uint32_t ipxlen;

	if(len < sizeof(*ipxhdr)){
		diagnostic("%s truncated (%zu < %zu) on %s",
				__func__,len,sizeof(*ipxhdr),op->i->name);
		op->malformed = 1;
		return;
	}
	ipxlen = ntohs(ipxhdr->ipx_pktsize);
	// Odd IPX packet lengths are padded to a 16-bit boundary, but this is
	// not reflected in the packet length field.
	if(ipxlen % 2){
		++ipxlen;
	}
	if(check_ethernet_padup(len,ipxlen)){
		diagnostic("%s malformed (%u != %zu) on %s",
				__func__,ipxlen,len,op->i->name);
		op->malformed = 1;
		return;
	}
	switch(ipxhdr->ipx_type){
	case IPX_TYPE_UNKNOWN:{
	break;}case IPX_TYPE_RIP:{
	break;}case IPX_TYPE_SAP:{
	break;}case IPX_TYPE_SPX:{
	break;}case IPX_TYPE_NCP:{
	break;}case IPX_TYPE_PPROP:{
	break;}default:{
		diagnostic("%s noproto %u on %s",
				__func__,ipxhdr->ipx_type,op->i->name);
		op->noproto = 1;
	break;} }
}
