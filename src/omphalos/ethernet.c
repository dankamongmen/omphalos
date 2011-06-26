#include <sys/socket.h>
#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <omphalos/util.h>
#include <linux/if_fddi.h>
#include <omphalos/eapol.h>
#include <linux/if_ether.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>
#include <linux/llc.h>

#define ETH_P_ECTP	0x9000	// Ethernet Configuration Test Protocol

static void
handle_ectp_packet(const omphalos_iface *octx __attribute__ ((unused)),
			interface *i __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

// 802.1q VLAN-tagged Ethernet II consists of:
//  12 bytes source/dest
//  4 byte 802.1q tag: 16-bit TPID (0x8100), 3-bit PCP, 1-bit CFI, 12-bit VID
//  2 byte EtherType
// This must be a real EtherType -- it cannot be an IEEE 802.2/SNAP length, nor
//  an 802.1q/802.1ad TPID. SNAP can use 802.1q, but does so by inserting the
//  802.1q tag between the OUI and EtherType (IEEE 802.2 without SNAP does not
//  have a SAP associated with 802.1q, and must use SNAP). 802.1q is inserted
//  before the EtherType whether using Ethernet II or SNAP. 802.1q does not
//  encapsulate itself (it could); 802.1ad must be used.
// Since 802.1q is always inserted before the EtherType, we define an "802.1q
//  header" to be the 6 bytes at the end of any Layer 2 encapulsation. frame
//  and len ought correspond to this, and op->l2s/op->l2d ought already be set.
#define IEEE8021QHDRLEN 6
static void
handle_8021q(const omphalos_iface *octx,interface *i,omphalos_packet *op,
				const void *frame,size_t len){
	const unsigned char *type;
	const void *dgram;
	size_t dlen;
	
	if(len < IEEE8021QHDRLEN){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	type = ((const unsigned char *)frame + 4);
 	dgram = (const char *)frame + IEEE8021QHDRLEN;
	dlen = len - IEEE8021QHDRLEN;
	op->l3proto = ntohs(*(const uint16_t *)type);
	switch(op->l3proto){
	case ETH_P_IP:{
		handle_ipv4_packet(octx,i,dgram,dlen);
	break;}case ETH_P_ARP:{
		handle_arp_packet(octx,i,dgram,dlen);
	break;}case ETH_P_IPV6:{
		handle_ipv6_packet(octx,i,dgram,dlen);
	break;}case ETH_P_PAE:{
		handle_eapol_packet(octx,i,dgram,dlen);
	break;}case ETH_P_ECTP:{
		handle_ectp_packet(octx,i,dgram,dlen);
	break;}default:{
		++i->noprotocol;
		octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,op->l3proto);
	break;} }
}

static void
handle_snap(const omphalos_iface *octx,interface *i,omphalos_packet *op,
					const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	const struct fddi_snap_hdr *snap;
	const void *dgram;
	uint16_t proto;
	size_t dlen;

	if(len < sizeof(*hdr) + sizeof(*snap)){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	snap = frame + sizeof(*hdr);
	dgram = (const char *)frame + sizeof(*hdr) + sizeof(*snap);
	dlen = len - sizeof(*hdr) - sizeof(*snap);
	// FIXME need handle IEEE 802.1ad doubly-tagged frames (and likely do
	// other crap involving DSAP/SSAP, OUI, etc)
       	proto = ntohs(snap->ethertype);
	op->l3proto = proto;
	switch(proto){
		case ETH_P_IP:{
			handle_ipv4_packet(octx,i,dgram,dlen);
			break;
		}case ETH_P_ARP:{
			handle_arp_packet(octx,i,dgram,dlen);
			break;
		}case ETH_P_IPV6:{
			handle_ipv6_packet(octx,i,dgram,dlen);
			break;
		}case ETH_P_8021Q:{	// 802.1q on SNAP
			handle_8021q(octx,i,op,dgram - IEEE8021QHDRLEN,
						dlen + IEEE8021QHDRLEN);
			break; // will modify op->l3proto
		}case ETH_P_PAE:{
			handle_eapol_packet(octx,i,dgram,dlen);
			break;
		}case ETH_P_ECTP:{
			handle_ectp_packet(octx,i,dgram,dlen);
			break;
		}default:{
			++i->noprotocol;
			octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,proto);
			break;
		}
	}
}

static void
handle_8022(const omphalos_iface *octx,interface *i,omphalos_packet *op,
					const void *frame,size_t len){
	const struct fddi_8022_2_hdr *llc;
	const struct ethhdr *hdr = frame;
	uint8_t sap;

	if(len < sizeof(*hdr) + sizeof(*llc)){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	sap = *((const unsigned char *)frame + sizeof(*hdr));
	if(sap == LLC_SAP_SNAP){
		handle_snap(octx,i,op,frame,len);
	}else{ // 802.1q always uses SNAP with OUI 00-00-00
		const void *dgram = (const char *)frame + sizeof(*hdr) + sizeof(*llc);
		size_t dlen = len - sizeof(*hdr) - sizeof(*llc);

		switch(sap){
			case LLC_SAP_IP:{
				op->l3proto = ETH_P_IP;
				handle_ipv4_packet(octx,i,dgram,dlen);
				break;
			}default:{ // IPv6 always uses SNAP per RFC2019
				++i->noprotocol;
				octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,sap);
				break;
			}
		}
	}
}

void handle_ethernet_packet(const omphalos_iface *octx,interface *i,omphalos_packet *op,
					const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	uint16_t proto;

	if(len < sizeof(*hdr)){
		++i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	// Source and dest immediately follow the preamble in all frame types
	op->l2s = lookup_l2host(&i->l2hosts,hdr->h_source,ETH_ALEN);
	op->l2d = lookup_l2host(&i->l2hosts,hdr->h_dest,ETH_ALEN);
       	proto = ntohs(hdr->h_proto);
	if(proto < 1536){		// 802.2 DSAP (and maybe SNAP/802.1q)
		// FIXME check the proto (LLC length) field against framelen!
		handle_8022(octx,i,op,frame,len); // modifies op->l3proto
	}else{				// Ethernet II
		const void *dgram = (const char *)frame + sizeof(*hdr);
		size_t dlen = len - sizeof(*hdr);

		op->l2s = lookup_l2host(&i->l2hosts,hdr->h_source,ETH_ALEN);
		op->l2d = lookup_l2host(&i->l2hosts,hdr->h_dest,ETH_ALEN);
		op->l3proto = proto;
		// FIXME need handle IEEE 802.1ad doubly-tagged frames
		switch(proto){
			case ETH_P_IP:{
				handle_ipv4_packet(octx,i,dgram,dlen);
				break;
			}case ETH_P_ARP:{
				handle_arp_packet(octx,i,dgram,dlen);
				break;
			}case ETH_P_IPV6:{
				handle_ipv6_packet(octx,i,dgram,dlen);
				break;
			}case ETH_P_8021Q:{	// 802.1q on Ethernet II
				handle_8021q(octx,i,op,dgram - IEEE8021QHDRLEN,
							dlen + IEEE8021QHDRLEN);
				break; // will modify op->l3proto
			}case ETH_P_PAE:{
				handle_eapol_packet(octx,i,dgram,dlen);
				break;
			}case ETH_P_ECTP:{
				handle_ectp_packet(octx,i,dgram,dlen);
				break;
			}default:{
				++i->noprotocol;
				octx->diagnostic("%s %s noproto for 0x%x",__func__,i->name,proto);
				break;
			}
		}
	}
}
