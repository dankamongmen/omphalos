#include <assert.h>
#include <netinet/ip.h>
#include <linux/tcp.h>
#include <linux/igmp.h>
#include <linux/l2tp.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/udp.h>
#include <omphalos/tcp.h>
#include <omphalos/gre.h>
#include <omphalos/pim.h>
#include <omphalos/sctp.h>
#include <omphalos/csum.h>
#include <omphalos/icmp.h>
#include <omphalos/ospf.h>
#include <omphalos/vrrp.h>
#include <omphalos/util.h>
#include <omphalos/cisco.h>
#include <omphalos/ipsec.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define DEFAULT_IP4_TTL 64
#define DEFAULT_IP6_TTL 64
#define IPPROTO_DSR	48 // Dynamic Source Routing, RFC 4728
#define IPPROTO_EIGRP	88 // Cisco EIGRP
#define IPPROTO_VRRP	112

typedef struct dsrhdr {
	uint8_t nxthdr;
	struct {
		unsigned fbit: 1;
		unsigned reserved: 7;
	} fres;
	uint16_t plen;
} __attribute__ ((packed)) dsrhdr;

static void
handle_dsr_packet(omphalos_packet *op,const void *frame,size_t len){
	const dsrhdr *dsr = frame;

	if(len < sizeof(*dsr)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

static void
handle_l2tp_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len);
	// FIXME
}

static void
handle_igmp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct igmphdr *igmp = frame;

	if(len < sizeof(*igmp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

void handle_ipv6_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct ip6_hdr *ip = frame;
	uint16_t plen;
	unsigned ver;
	uint8_t next;

	if(len < sizeof(*ip)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu on %s",__func__,len,op->i->name);
		return;
	}
	ver = ntohl(ip->ip6_ctlun.ip6_un1.ip6_un1_flow) >> 28u;
	if(ver != 6){
		op->noproto = 1;
		diagnostic("%s noversion for %u on %s",__func__,ver,op->i->name);
		return;
	}
	plen = ntohs(ip->ip6_ctlun.ip6_un1.ip6_un1_plen);
	if(len < plen + sizeof(*ip)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu != %u on %s",__func__,len,plen,op->i->name);
		return;
	}
	memcpy(op->l3saddr,&ip->ip6_src,16);
	memcpy(op->l3daddr,&ip->ip6_dst,16);
	op->l3s = lookup_l3host(&op->tv,op->i,op->l2s,AF_INET6,&ip->ip6_src);
	op->l3d = lookup_l3host(&op->tv,op->i,op->l2d,AF_INET6,&ip->ip6_dst);
	// Don't just subtract payload length from frame length, since the
	// frame might have been padded up to a minimum size.
	const void *nhdr = (const char *)ip + sizeof(*ip);
	next = ip->ip6_ctlun.ip6_un1.ip6_un1_nxt;
	while(nhdr){
	switch(next){ // "upper-level" protocols end the packet
		case IPPROTO_TCP:{
			handle_tcp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_UDP:{
			handle_udp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_ICMP:{
			handle_icmp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_ICMP6:{
			handle_icmp6_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_SCTP:{
			handle_sctp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_GRE:{
			handle_gre_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_IGMP:{
			handle_igmp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_PIM:{
			handle_pim_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_DSR:{
			handle_dsr_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_VRRP:{
			handle_vrrp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_EIGRP:{
			handle_eigrp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_ESP:{
			handle_esp_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_AH:{
			handle_ah_packet(op,nhdr,plen);
			nhdr = NULL;
		break; }case IPPROTO_HOPOPTS:{
			const struct ip6_ext *opt = nhdr;

			if(plen < sizeof(*opt) || plen < (opt->ip6e_len + 1) * 8){
				op->malformed = 1;
				diagnostic("%s malformed with len %d on %s",__func__,plen,op->i->name);
				return;
			}
			plen -= (opt->ip6e_len + 1) * 8;
			nhdr = (const char *)nhdr + (opt->ip6e_len + 1) * 8;
			next = opt->ip6e_nxt;
		break; }case IPPROTO_FRAGMENT:{
			const struct ip6_frag *opt = nhdr;

			if(plen < sizeof(*opt)){
				op->malformed = 1;
				diagnostic("%s malformed with len %d on %s",__func__,plen,op->i->name);
				return;
			}
			plen -= sizeof(*opt);
			nhdr = (const char *)nhdr + sizeof(*opt);
			next = opt->ip6f_nxt;
			return; // FIXME reassemble fragments!
		break; }default:{
			op->noproto = 1;
			diagnostic("%s %s noproto for %u",__func__,
					op->i->name,next);
			return;
		break; } }
	}
}

void handle_ipv4_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct iphdr *ip = frame;
	unsigned hlen;

	if(len < sizeof(*ip)){
		op->malformed = 1;
		diagnostic("[%s] ipv4 malformed with %zu",op->i->name,len);
		return;
	}
	hlen = ip->ihl << 2u;
	if(len < hlen){
		op->malformed = 1;
		diagnostic("[%s] ipv4 malformed with %zu vs %u",op->i->name,len,hlen);
		return;
	}
	if(!hlen){
		op->malformed = 1;
		diagnostic("[%s] ipv4 malformed with 0 hdrlen",op->i->name);
		return;
	}
	if(ipv4_csum(frame)){
		op->malformed = 1;
		diagnostic("[%s] bad IPv4 checksum (%04hx)",op->i->name,ipv4_csum(frame));
		return;
	}
	if(ip->version != 4){
		op->noproto = 1;
		diagnostic("[%s] ipv4 noversion for %u",op->i->name,ip->version);
		return;
	}
	// len can be greater than tot_len due to layer 2 padding requirements
	if(len < ntohs(ip->tot_len)){
		op->malformed = 1;
		diagnostic("[%s] malformed with %zu vs %hu",op->i->name,len,ntohs(ip->tot_len));
		return;
	}
	memcpy(op->l3saddr,&ip->saddr,4);
	memcpy(op->l3daddr,&ip->daddr,4);
	op->l3s = lookup_l3host(&op->tv,op->i,op->l2s,AF_INET,&ip->saddr);
	op->l3d = lookup_l3host(&op->tv,op->i,op->l2d,AF_INET,&ip->daddr);

	// FIXME need reassemble fragments
	if((ip->frag_off & __constant_ntohs(0x1f)) || (ip->frag_off & __constant_ntohs(0x20))){
		return;
	}

	const void *nhdr = (const unsigned char *)frame + hlen;
	const size_t nlen = ntohs(ip->tot_len) - hlen;

	switch(ip->protocol){
	case IPPROTO_TCP:{
		handle_tcp_packet(op,nhdr,nlen);
	break; }case IPPROTO_UDP:{
		handle_udp_packet(op,nhdr,nlen);
	break; }case IPPROTO_ICMP:{
		handle_icmp_packet(op,nhdr,nlen);
	break; }case IPPROTO_SCTP:{
		handle_sctp_packet(op,nhdr,nlen);
	break; }case IPPROTO_GRE:{
		handle_gre_packet(op,nhdr,nlen);
	break; }case IPPROTO_IGMP:{
		handle_igmp_packet(op,nhdr,nlen);
	break; }case IPPROTO_L2TP:{
		handle_l2tp_packet(op,nhdr,nlen);
	break; }case IPPROTO_OSPF:{
		handle_ospf_packet(op,nhdr,nlen);
	break; }case IPPROTO_PIM:{
		handle_pim_packet(op,nhdr,nlen);
	break; }case IPPROTO_DSR:{
		handle_dsr_packet(op,nhdr,nlen);
	break; }case IPPROTO_VRRP:{
		handle_vrrp_packet(op,nhdr,nlen);
	break; }case IPPROTO_EIGRP:{
		handle_eigrp_packet(op,nhdr,nlen);
	break; }case IPPROTO_ESP:{
		handle_esp_packet(op,nhdr,nlen);
	break; }case IPPROTO_AH:{
		handle_ah_packet(op,nhdr,nlen);
	break; }case IPPROTO_IPV6:{
		handle_ipv6_packet(op,nhdr,nlen);
	break; }default:{
		op->noproto = 1;
		diagnostic("[%s] ipv4 noproto for %u",op->i->name,ip->protocol);
	break; } }
}

// Doesn't set ->tot_len; that must be done by the caller. Prepare ->check for
// checksum evaluation, but we cannot yet actually evaluate it (FIXME though we
// could calculate differential).
int prep_ipv4_header(void *frame,size_t flen,uint32_t src,uint32_t dst,unsigned proto){
	struct iphdr *ip;

	if(flen < sizeof(*ip)){
		return -1;
	}
	ip = frame;
	memset(ip,0,sizeof(*ip));
	ip->version = 4;
	ip->ihl = sizeof(*ip) >> 2u;
	ip->ttl = DEFAULT_IP4_TTL;
	ip->id = random();
	ip->saddr = src;
	if((ntohl(ip->daddr = dst) & 0xf0000000u) == 0xe0000000){
		ip->ttl = 1;
	}
	ip->protocol = proto;
	return ip->ihl << 2u;
}

// Doesn't set ->tot_len; that must be done by the caller. Prepare ->check for
// checksum evaluation, but we cannot yet actually evaluate it (FIXME though we
// could calculate differential).
int prep_ipv6_header(void *frame,size_t flen,const uint128_t src,
			const uint128_t dst,unsigned proto){
	struct ip6_hdr *ip;

	if(flen < sizeof(*ip)){
		return -1;
	}
	ip = frame;
	memset(ip,0,sizeof(*ip));
	ip->ip6_ctlun.ip6_un1.ip6_un1_flow = htonl(6u << 28u);
	ip->ip6_ctlun.ip6_un1.ip6_un1_hlim = DEFAULT_IP6_TTL;
	ip->ip6_ctlun.ip6_un1.ip6_un1_nxt = proto;
	cast128(ip->ip6_src.s6_addr32,src);
	cast128(ip->ip6_dst.s6_addr32,dst);
	return sizeof(*ip);
}
