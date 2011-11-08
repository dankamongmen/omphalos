#include <sys/socket.h>
#include <linux/if.h>
#include <linux/llc.h>
#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <omphalos/ipx.h>
#include <omphalos/util.h>
#include <linux/if_fddi.h>
#include <omphalos/eapol.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define ETH_P_LLDP	0x88cc	// Link Layer Discovery Protocol
#define ETH_P_ECTP	0x9000	// Ethernet Configuration Test Protocol

#define LLC_MAX_LEN	1536 // one more than maximum length of 802.2 LLC

static void
handle_lldp_packet(const omphalos_iface *octx __attribute__ ((unused)),
			omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_ectp_packet(const omphalos_iface *octx __attribute__ ((unused)),
			omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_stp_packet(const omphalos_iface *octx __attribute__ ((unused)),
			omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_osi_packet(const omphalos_iface *octx __attribute__ ((unused)),
			omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_8022(const omphalos_iface *,omphalos_packet *,const void *,size_t);

// 802.1q VLAN-tagged Ethernet II consists of:
//  12 bytes source/dest
//  4 byte 802.1q tag: 16-bit TPID (0x8100), 3-bit PCP, 1-bit CFI, 12-bit VID
//  2 byte EtherType
// This must be a real EtherType -- it cannot be an IEEE 802.2/SNAP length, nor
//  an 802.1q/802.1ad TPID (but see below re 'allowllc'). SNAP can use 802.1q,
//  but does so by inserting the 802.1q tag between the OUI and EtherType (IEEE
//  802.2 without SNAP does not have a SAP associated with 802.1q, and must use
//  SNAP). 802.1q is inserted before the EtherType whether using Ethernet II or
//  SNAP. 802.1q does not encapsulate itself (it could); 802.1ad must be used.
// Since 802.1q is always inserted before the EtherType, we define an "802.1q
//  header" to be the 6 bytes at the end of any Layer 2 encapulsation. frame
//  and len ought correspond to this, and op->l2s/op->l2d ought already be set.
#define IEEE8021QHDRLEN 6
static void
handle_8021q(const omphalos_iface *octx,omphalos_packet *op,const void *frame,
					size_t len,int allowllc){
	const unsigned char *type;
	const void *dgram;
	size_t dlen;
	
	if(len < IEEE8021QHDRLEN){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		return;
	}
	type = ((const unsigned char *)frame + 4);
 	dgram = (const char *)frame + IEEE8021QHDRLEN;
	dlen = len - IEEE8021QHDRLEN;
	op->l3proto = ntohs(*(const uint16_t *)type);
	switch(op->l3proto){
	case ETH_P_IP:{
		handle_ipv4_packet(octx,op,dgram,dlen);
	break;}case ETH_P_ARP:{
		handle_arp_packet(octx,op,dgram,dlen);
	break;}case ETH_P_IPV6:{
		handle_ipv6_packet(octx,op,dgram,dlen);
	break;}case ETH_P_PAE:{
		handle_eapol_packet(octx,op,dgram,dlen);
	break;}case ETH_P_IPX:{
		handle_ipx_packet(octx,op,dgram,dlen);
	break;}case ETH_P_ECTP:{
		handle_ectp_packet(octx,op,dgram,dlen);
	break;}default:{
		// At least Cisco PVST BDPU's under VLAN use 802.1q to directly
		// encapsulate IEEE 802.2/SNAP. See:
		// http://www.ciscopress.com/articles/article.asp?p=1016582
		if(allowllc && op->l3proto < LLC_MAX_LEN){
			handle_8022(octx,op,frame,len);
		}else{
			op->noproto = 1;
			octx->diagnostic(L"%s %s noproto for 0x%x",__func__,
					op->i->name,op->l3proto);
		}
	break;} }
}

static void
handle_snap(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct fddi_snap_hdr *snap = frame;
	const void *dgram;
	uint16_t proto;
	size_t dlen;

	if(len < sizeof(*snap)){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		return;
	}
	dgram = (const char *)frame + sizeof(*snap);
	if(snap->ssap != LLC_SAP_SNAP || snap->ctrl != 0x03){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed ssap/ctrl %zu/%zu",__func__,snap->ssap,snap->ctrl);
		return;
	}
	dlen = len - sizeof(*snap);
	// FIXME need handle IEEE 802.1ad doubly-tagged frames (and likely do
	// other crap involving OUI, etc)
       	proto = ntohs(snap->ethertype);
	op->l3proto = proto;
	switch(proto){
		case ETH_P_IP:{
			handle_ipv4_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_ARP:{
			handle_arp_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_IPV6:{
			handle_ipv6_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_8021Q:{	// 802.1q on SNAP
			handle_8021q(octx,op,dgram - IEEE8021QHDRLEN,
						dlen + IEEE8021QHDRLEN,0);
			break; // will modify op->l3proto
		}case ETH_P_PAE:{
			handle_eapol_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_ECTP:{
			handle_ectp_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_IPX:{
			handle_ipx_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_LLDP:{
			handle_lldp_packet(octx,op,dgram,dlen);
			break;
		}default:{
			op->noproto = 1;
			octx->diagnostic(L"%s %s noproto for 0x%x",__func__,
					op->i->name,proto);
			break;
		}
	}
}

static void
handle_8022(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct fddi_8022_1_hdr *llc = frame;
	uint8_t sap;

	if(len < sizeof(*llc)){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		return;
	}
	sap = llc->dsap;
	if(sap == LLC_SAP_SNAP){
		handle_snap(octx,op,frame,len);
	}else{ // 802.1q always uses SNAP with OUI 00-00-00
		const void *dgram = (const char *)frame + sizeof(*llc);
		size_t dlen = len - sizeof(*llc);

		// Unnumbered (most common): 11xxxxxx
		// Supervisory: 10xxxxxx
		// Information: 0xxxxxxx
		if(((llc->ctrl & 0x3u) == 0x1u) || ((llc->ctrl & 0x3u) == 0x0)){
			if(dlen == 0){
				op->malformed = 1;
				octx->diagnostic(L"%s malformed with %zu",__func__,len);
				return;
			}
			++dgram;
			--dlen;
		}
		switch(sap){
			case LLC_SAP_IP:{
				op->l3proto = ETH_P_IP;
				handle_ipv4_packet(octx,op,dgram,dlen);
				break;
			}case LLC_SAP_BSPAN:{ // STP
				op->l3proto = ETH_P_STP;
				handle_stp_packet(octx,op,dgram,dlen);
				break;
			}case LLC_SAP_OSI:{	// Routed OSI PDU
				op->l3proto = ETH_P_OSI;
				handle_osi_packet(octx,op,dgram,dlen);
				break;
			}case LLC_SAP_IPX:{
				op->l3proto = ETH_P_IPX;
				handle_ipx_packet(octx,op,dgram,dlen);
				break;
			}default:{ // IPv6 always uses SNAP per RFC2019
				op->noproto = 1;
				octx->diagnostic(L"%s %s noproto for 0x%x",__func__,
						op->i->name,sap);
				break;
			}
		}
	}
}

void handle_ethernet_packet(const omphalos_iface *octx,omphalos_packet *op,
					const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	const void *dgram;
	uint16_t proto;
	size_t dlen;

	if(len < sizeof(*hdr)){
		op->malformed = 1;
		octx->diagnostic(L"%s malformed with %zu",__func__,len);
		return;
	}
	// Source and dest immediately follow the preamble in all frame types
	op->l2s = lookup_l2host(op->i,hdr->h_source);
	op->l2d = lookup_l2host(op->i,hdr->h_dest);
	dgram = (const char *)frame + sizeof(*hdr);
	dlen = len - sizeof(*hdr);
       	proto = ntohs(hdr->h_proto);
	op->l3proto = proto;
	// FIXME need handle IEEE 802.1ad doubly-tagged frames
	switch(proto){
		case ETH_P_IP:{
			handle_ipv4_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_ARP:{
			handle_arp_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_IPV6:{
			handle_ipv6_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_8021Q:{	// 802.1q on Ethernet II
			handle_8021q(octx,op,(const char *)dgram - IEEE8021QHDRLEN,
						dlen + IEEE8021QHDRLEN,1);
			break; // will modify op->l3proto
		}case ETH_P_PAE:{
			handle_eapol_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_ECTP:{
			handle_ectp_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_IPX:{
			handle_ipx_packet(octx,op,dgram,dlen);
			break;
		}case ETH_P_LLDP:{
			handle_lldp_packet(octx,op,dgram,dlen);
			break;
		}default:{
			if(proto < LLC_MAX_LEN){ // 802.2 DSAP (and maybe SNAP/802.1q)
				// FIXME check the proto (LLC length) field against framelen!
				handle_8022(octx,op,(const char *)dgram,dlen); // modifies op->l3proto
			}else{
				op->noproto = 1;
				octx->diagnostic(L"%s %s noproto for 0x%x",__func__,
						op->i->name,proto);
			}
			break;
		}
	}
}

int prep_eth_header(void *frame,size_t len,const interface *i,const void *dst,
						uint16_t proto){
	struct ethhdr *e = frame;

	if(len < ETH_HLEN){
		return -1;
	}
	memcpy(&e->h_dest,dst,ETH_ALEN);
	memcpy(&e->h_source,i->addr,ETH_ALEN);
	e->h_proto = htons(proto);
	return ETH_HLEN;
}
