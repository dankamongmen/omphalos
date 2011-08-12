#include <netinet/ip.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/igmp.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/udp.h>
#include <omphalos/pim.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static void
handle_tcp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct tcphdr *tcp = frame;

	if(len < sizeof(*tcp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME check header len etc...
}

static void
handle_icmp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}

static void
handle_igmp_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct igmphdr *igmp = frame;

	if(len < sizeof(*igmp)){
		octx->diagnostic("%s malformed with %zu",__func__,len);
		++op->i->malformed;
		return;
	}
	// FIXME
}

void handle_ipv6_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const struct ip6_hdr *ip = frame;
	uint16_t plen;
	unsigned ver;

	if(len < sizeof(*ip)){
		++op->i->malformed;
		octx->diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	ver = ntohl(ip->ip6_ctlun.ip6_un1.ip6_un1_flow) >> 28u;
	if(ver != 6){
		++op->i->noprotocol;
		octx->diagnostic("%s noproto for %u",__func__,ver);
		return;
	}
	plen = ntohs(ip->ip6_ctlun.ip6_un1.ip6_un1_plen);
	if(len < plen + sizeof(*ip)){
		++op->i->malformed;
		octx->diagnostic("%s malformed with %zu != %u",__func__,len,plen);
		return;
	}
	// FIXME check extension headers...
	op->l3s = lookup_l3host(octx,op->i,op->l2s,AF_INET6,&ip->ip6_src);
	op->l3d = lookup_l3host(octx,op->i,op->l2d,AF_INET6,&ip->ip6_dst);
	const void *nhdr = (const unsigned char *)frame + (len - plen);

	// FIXME don't call down if we're fragmented
	switch(ip->ip6_ctlun.ip6_un1.ip6_un1_nxt){
	case IPPROTO_TCP:{
		handle_tcp_packet(octx,op,nhdr,plen);
	break; }case IPPROTO_UDP:{
		handle_udp_packet(octx,op,nhdr,plen);
	break; }case IPPROTO_ICMP:{
		handle_icmp_packet(octx,op,nhdr,plen);
	break; }case IPPROTO_IGMP:{
		handle_igmp_packet(octx,op,nhdr,plen);
	break; }case IPPROTO_PIM:{
		handle_pim_packet(octx,op,nhdr,plen);
	break; }default:{
		++op->i->noprotocol;
		octx->diagnostic("%s %s noproto for %u",__func__,
				op->i->name,ip->ip6_ctlun.ip6_un1.ip6_un1_nxt);
	break; } }
}

void handle_ipv4_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const struct iphdr *ip = frame;
	//struct iphost *ips,*ipd;
	unsigned hlen;

	if(len < sizeof(*ip)){
		++op->i->malformed;
		octx->diagnostic("%s %s malformed with %zu",__func__,
				op->i->name,len);
		return;
	}
	if(ip->version != 4){
		++op->i->noprotocol;
		octx->diagnostic("%s %s noversion for %u",__func__,
				op->i->name,ip->version);
		return;
	}
	hlen = ip->ihl << 2u;
	if(len < hlen){
		++op->i->malformed;
		octx->diagnostic("%s %s malformed with %zu vs %u",__func__,
				op->i->name,len,hlen);
		return;
	}
	if(check_ethernet_padup(len,ntohs(ip->tot_len))){
		++op->i->malformed;
		octx->diagnostic("%s %s malformed with %zu vs %hu",__func__,
				op->i->name,len,ntohs(ip->tot_len));
		return;
	}
	op->l3s = lookup_l3host(octx,op->i,op->l2s,AF_INET,&ip->saddr);
	op->l3d = lookup_l3host(octx,op->i,op->l2d,AF_INET,&ip->daddr);

	const void *nhdr = (const unsigned char *)frame + hlen;
	const size_t nlen = ntohs(ip->tot_len) - hlen;

	// FIXME don't call down if we're fragmented
	switch(ip->protocol){
	case IPPROTO_TCP:{
		handle_tcp_packet(octx,op,nhdr,nlen);
	break; }case IPPROTO_UDP:{
		handle_udp_packet(octx,op,nhdr,nlen);
	break; }case IPPROTO_ICMP:{
		handle_icmp_packet(octx,op,nhdr,nlen);
	break; }case IPPROTO_IGMP:{
		handle_igmp_packet(octx,op,nhdr,nlen);
	break; }case IPPROTO_PIM:{
		handle_pim_packet(octx,op,nhdr,nlen);
	break; }default:{
		++op->i->noprotocol;
		octx->diagnostic("%s %s noproto for %u",__func__,
				op->i->name,ip->protocol);
	break; } }
}
