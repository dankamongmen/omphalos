#include <net/if.h>
#include <arpa/inet.h>
#include <linux/llc.h>
//#include <netinet/in.h>
#include <sys/socket.h>
#include <omphalos/ip.h>
#include <omphalos/arp.h>
#include <omphalos/ipx.h>
#include <omphalos/stp.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/mpls.h>
#include <linux/if_fddi.h>
#include <omphalos/lltd.h>
#include <omphalos/eapol.h>
#include <linux/if_ether.h>
#include <linux/if_pppox.h>
#include <omphalos/cisco.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define ETH_P_ECTP 0x9000	// Ethernet Configuration Test Protocol
#define ETH_P_UDLD 0x0111	// Unidirectional Link Detection Protocol
#define ETH_P_CLD 0x2000  // Cisco Discovery Protocol
#define ETH_P_WOL 0x0842  // Wake-on-Lan (can be sent on any transport)

#define LLC_MAX_LEN	1536 // one more than maximum length of 802.2 LLC

// Wake-on-Lan
static void
handle_wol_frame(omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}


// IEEE 802.3 31B Pause frames
static void
handle_mac_frame(omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_lldp_packet(omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_ectp_packet(omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_osi_packet(omphalos_packet *op __attribute__ ((unused)),
			const void *frame __attribute__ ((unused)),
			size_t len __attribute__ ((unused))){
	// FIXME
}

static void
handle_8022(omphalos_packet *,const void *,size_t);

// 802.1q VLAN-tagged Ethernet II consists of:
//  12 bytes source/dest
//  4 byte 802.1q tag: 16-bit TPID (0x8100), 3-bit PCP, 1-bit CFI, 12-bit VID
//  2 byte EtherType
// This must be a real EtherType -- it cannot be an IEEE 802.2/SNAP length, nor
//  an 802.1q/802.1ad TPID (but see below re 'allowllc'). SNAP can use 802.1q,
//  but does so by inserting the 802.1q tag between the OUI and EtherType (IEEE
//  802.2 without SNAP does not have a SAP associated with 802.1q, and must use
//  SNAP). 802.1q is inserted before the EtherType whether using Ethernet II or
//  SNAP. 802.1q should not encapsulate itself (it could); 802.1ad ought be
//  used. Nonetheless, some (Cisco) implementations double up on 802.1q.
// Since 802.1q is always inserted before the EtherType, we define an "802.1q
//  header" to be the 6 bytes at the end of any Layer 2 encapulsation. frame
//  and len ought correspond to this, and op->l2s/op->l2d ought already be set.
#define IEEE8021QHDRLEN 6
static void
handle_8021q(omphalos_packet *op,const void *frame,size_t len,int allowllc){
	const unsigned char *type;
	const void *dgram;
	size_t dlen;
	
	if(len < IEEE8021QHDRLEN){
		op->malformed = 1;
		diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	type = ((const unsigned char *)frame + 4);
 	dgram = (const char *)frame + IEEE8021QHDRLEN;
	dlen = len - IEEE8021QHDRLEN;
	op->l3proto = ntohs(*(const uint16_t *)type);
	switch(op->l3proto){
	case ETH_P_IP:{
		handle_ipv4_packet(op,dgram,dlen);
	break;}case ETH_P_ARP:{
		handle_arp_packet(op,dgram,dlen);
	break;}case ETH_P_IPV6:{
		handle_ipv6_packet(op,dgram,dlen);
	break;}case ETH_P_PAE:{
		handle_eapol_packet(op,dgram,dlen);
	break;}case ETH_P_IPX:{
		handle_ipx_packet(op,dgram,dlen);
	break;}case ETH_P_ECTP:{
		handle_ectp_packet(op,dgram,dlen);
	break;}case ETH_P_LLDP:{
		handle_lldp_packet(op,dgram,dlen);
	break;}case ETH_P_UDLD:{
		handle_udld_packet(op,dgram,dlen);
	break;}case ETH_P_DTP:{
		handle_dtp_packet(op,dgram,dlen);
	break;}case ETH_P_8021Q:{
		// 802.1q-under-802.1q; we need consider the 16-bit Type field
		// to be part of the following 802.1q TPID. Account for it.
		handle_8021q(op,dgram - 2,dlen + 2,0);
	break;}default:{
		// At least Cisco PVST BDPU's under VLAN use 802.1q to directly
		// encapsulate IEEE 802.2/SNAP. See:
		// http://www.ciscopress.com/articles/article.asp?p=1016582
		if(allowllc && op->l3proto < LLC_MAX_LEN){
			handle_8022(op,dgram,dlen);
		}else{
			op->noproto = 1;
			diagnostic("%s %s noproto for 0x04%x",__func__,
					op->i->name,op->l3proto);
		}
	break;} }
}

static void
handle_snap(omphalos_packet *op,const void *frame,size_t len){
	const struct fddi_snap_hdr *snap = frame;
	const void *dgram;
	uint16_t proto;
	size_t dlen;

	if(len < sizeof(*snap)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	dgram = (const char *)frame + sizeof(*snap);
	if(snap->ssap != LLC_SAP_SNAP || snap->ctrl != 0x03){
		op->malformed = 1;
		diagnostic("%s malformed ssap/ctrl %d/%d",__func__,snap->ssap,snap->ctrl);
		return;
	}
	dlen = len - sizeof(*snap);
	// FIXME need handle IEEE 802.1ad doubly-tagged frames (and likely do
	// other crap involving OUI, etc)
       	proto = ntohs(snap->ethertype);
	// FIXME also, things like Cisco ISL do an encapsulation and you don't
	// know about it except by checking the dest address (01:00:0c:cc:cc:cc)
	op->l3proto = proto;
	switch(proto){
		case ETH_P_IP:{
			handle_ipv4_packet(op,dgram,dlen);
			break;
		}case ETH_P_ARP:{
			handle_arp_packet(op,dgram,dlen);
			break;
		}case ETH_P_IPV6:{
			handle_ipv6_packet(op,dgram,dlen);
			break;
		}case ETH_P_8021Q:{	// 802.1q on SNAP
			handle_8021q(op,dgram - IEEE8021QHDRLEN,
						dlen + IEEE8021QHDRLEN,0);
			break; // will modify op->l3proto
		}case ETH_P_PAE:{
			handle_eapol_packet(op,dgram,dlen);
			break;
		}case ETH_P_ECTP:{
			handle_ectp_packet(op,dgram,dlen);
			break;
		}case ETH_P_IPX:{
			handle_ipx_packet(op,dgram,dlen);
			break;
		}case ETH_P_LLDP:{
			handle_lldp_packet(op,dgram,dlen);
			break;
		}case ETH_P_CLD:{
			handle_cld_packet(op,dgram,dlen);
			break;
		}case ETH_P_UDLD:{
			handle_udld_packet(op,dgram,dlen);
			break;
		}case ETH_P_DTP:{
			handle_dtp_packet(op,dgram,dlen);
			break;
		}default:{
			op->noproto = 1;
			diagnostic("%s %s noproto for 0x%04x",__func__,
					op->i->name,proto);
			break;
		}
	}
}

static void
handle_8022(omphalos_packet *op,const void *frame,size_t len){
	const struct fddi_8022_1_hdr *llc = frame;
	uint8_t sap;

	if(len < sizeof(*llc)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	sap = llc->dsap;
	if(sap == LLC_SAP_SNAP){
		handle_snap(op,frame,len);
	}else{ // 802.1q always uses SNAP with OUI 00-00-00
		const void *dgram = (const char *)frame + sizeof(*llc);
		size_t dlen = len - sizeof(*llc);

		// Unnumbered (most common): 11xxxxxx
		// Supervisory: 10xxxxxx
		// Information: 0xxxxxxx
		if(((llc->ctrl & 0x3u) == 0x1u) || ((llc->ctrl & 0x3u) == 0x0)){
			if(dlen == 0){
				op->malformed = 1;
				diagnostic("%s malformed with %zu",__func__,len);
				return;
			}
			++dgram;
			--dlen;
		}
		switch(sap){
			case LLC_SAP_IP:{
				op->l3proto = ETH_P_IP;
				handle_ipv4_packet(op,dgram,dlen);
				break;
			}case LLC_SAP_BSPAN:{ // STP
				op->l3proto = ETH_P_STP;
				handle_stp_packet(op,dgram,dlen);
				break;
			}case LLC_SAP_OSI:{	// Routed OSI PDU
				op->l3proto = ETH_P_OSI;
				handle_osi_packet(op,dgram,dlen);
				break;
			}case LLC_SAP_IPX:{
				op->l3proto = ETH_P_IPX;
				handle_ipx_packet(op,dgram,dlen);
				break;
			}case LLC_SAP_NULL:{
				// Used for local testing etc
				op->l3proto = 0; // uhhhh
				break;
			}default:{ // IPv6 always uses SNAP per RFC2019
				op->noproto = 1;
				diagnostic("%s %s noproto for 0x02%x",__func__,
						op->i->name,sap);
				break;
			}
		}
	}
}

static void
handle_pppoe_packet(omphalos_packet *op,const void *frame,size_t len){
	// Works for both PPPoE Discovery and PPPoE Session
	const struct pppoe_hdr *ppp = frame;
	size_t dlen;

	if(len < sizeof(*ppp)){
		op->malformed = 1;
		diagnostic("%s %s malformed with %zu",op->i->name,__func__,len);
		return;
	}
	dlen = len - sizeof(*ppp);
	if(dlen < ntohs(ppp->length)){
		op->malformed = 1;
		diagnostic("%s %s malformed with %zu",op->i->name,__func__,len);
		return;
	}
	// FIXME
}

void handle_l2tun_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len);
	// FIXME not safe to hand this down without setting up op->l2s etc...
	//handle_ipv4_packet(op,frame,len);
}

void handle_ethernet_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct ethhdr *hdr = frame;
	const void *dgram;
	uint16_t proto;
	size_t dlen;

	if(len < sizeof(*hdr)){
		op->malformed = 1;
		diagnostic("%s %s malformed with %zu",op->i->name,__func__,len);
		return;
	}
	// Source and dest immediately follow the preamble in all frame types
	op->l2s = lookup_l2host(op->i,hdr->h_source);
	op->l2d = lookup_l2host(op->i,hdr->h_dest);
	dgram = (const char *)frame + sizeof(*hdr);
	dlen = len - sizeof(*hdr);
       	proto = ntohs(hdr->h_proto);
	op->l3proto = proto;
	op->pcap_ethproto = proto;
	// FIXME need handle IEEE 802.1ad doubly-tagged frames
	switch(proto){
		case ETH_P_IP:{
			handle_ipv4_packet(op,dgram,dlen);
			break;
		}case ETH_P_ARP:{
			handle_arp_packet(op,dgram,dlen);
			break;
		}case ETH_P_IPV6:{
			handle_ipv6_packet(op,dgram,dlen);
			break;
		}case ETH_P_8021Q:{// 802.1q on Ethernet II. Account for TPID.
			handle_8021q(op,(const char *)dgram - 2,dlen + 2,1);
			break; // will modify op->l3proto
		}case ETH_P_PAE:{
			handle_eapol_packet(op,dgram,dlen);
			break;
		}case ETH_P_ECTP:{
			handle_ectp_packet(op,dgram,dlen);
			break;
		}case ETH_P_IPX:{
			handle_ipx_packet(op,dgram,dlen);
			break;
		}case ETH_P_LLTD:{
			handle_lltd_packet(op,dgram,dlen);
			break;
		}case ETH_P_LLDP:{
			handle_lldp_packet(op,dgram,dlen);
			break;
		}case ETH_P_UDLD:{
			handle_udld_packet(op,dgram,dlen);
			break;
		}case ETH_P_DTP:{
			handle_dtp_packet(op,dgram,dlen);
			break;
		}case ETH_P_PPP_DISC:
		 case ETH_P_PPP_SES:{
			handle_pppoe_packet(op,dgram,dlen);
			break;
		}case ETH_P_MPLS_UC:{
			handle_mpls_packet(op,dgram,dlen);
			break;
		}case ETH_P_PAUSE:{
			handle_mac_frame(op,dgram,dlen);
			break;
		}case ETH_P_WOL:{
			handle_wol_frame(op,dgram,dlen);
			break;
		}case ETH_P_DEC: // 0x6000..0x6007 are all DEC jankware
		case ETH_P_DNA_DL:
		case ETH_P_DNA_RC:
		case ETH_P_DNA_RT:
		case ETH_P_LAT:
		case ETH_P_DIAG:
		case ETH_P_CUST:
		case ETH_P_SCA:{
			//handle_dec_packet(op,dgram,dlen); FIXME
			break;
		}default:{
			if(proto < LLC_MAX_LEN){ // 802.2 DSAP (and maybe SNAP/802.1q)
				// FIXME check the proto (LLC length) field against framelen!
				handle_8022(op,(const char *)dgram,dlen); // modifies op->l3proto
				op->pcap_ethproto = 4;
			}else{
				op->noproto = 1;
				diagnostic("%s %s noproto for 0x04%x",__func__,
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
	if(i->addrlen != ETH_ALEN){
		return 0;
	}
	memcpy(&e->h_dest,dst,ETH_ALEN);
	memcpy(&e->h_source,i->addr,ETH_ALEN);
	e->h_proto = htons(proto);
	return ETH_HLEN;
}
