#include <bits/sockaddr.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/igmp.h>
#include <linux/ipv6.h>
#include <omphalos/ip.h>
#include <omphalos/util.h>
#include <omphalos/ethernet.h>
#include <omphalos/netaddrs.h>
#include <omphalos/interface.h>

void handle_ipv6_packet(interface *i,const void *frame,size_t len){
	const struct ipv6hdr *ip = frame;

	if(len < sizeof(*ip)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	if(ip->version != 6){
		printf("%s noproto for %u\n",__func__,ip->version);
		++i->noprotocol;
		return;
	}
	if(len != be16toh(ip->payload_len) + sizeof(*ip)){
		printf("%s malformed with %zu != %zu\n",__func__,len,be16toh(ip->payload_len) + sizeof(*ip));
		++i->malformed;
		return;
	}
	// FIXME check extension headers...
	// FIXME...
}

static void
handle_tcp_packet(interface *i,const void *frame,size_t len){
	const struct tcphdr *tcp = frame;

	if(len < sizeof(*tcp)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME check header len etc...
}

static void
handle_udp_packet(interface *i,const void *frame,size_t len){
	const struct udphdr *udp = frame;

	if(len < sizeof(*udp)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME
}

static void
handle_icmp_packet(interface *i,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME
}

static void
handle_igmp_packet(interface *i,const void *frame,size_t len){
	const struct igmphdr *igmp = frame;

	if(len < sizeof(*igmp)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME
}

void handle_ipv4_packet(interface *i,const void *frame,size_t len){
	const struct iphdr *ip = frame;
	struct iphost *ips,*ipd;
	unsigned hlen;

	if(len < sizeof(*ip)){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	if(ip->version != 4){
		printf("%s noproto for %u\n",__func__,ip->version);
		++i->noprotocol;
		return;
	}
	hlen = ip->ihl << 2u;
	if(len < hlen){
		printf("%s malformed with %zu vs %u\n",__func__,len,hlen);
		++i->malformed;
		return;
	}
	if(check_ethernet_padup(len,be16toh(ip->tot_len))){
		printf("%s malformed with %zu vs %hu\n",__func__,len,be16toh(ip->tot_len));
		++i->malformed;
		return;
	}
	if( (ips = lookup_iphost(&ip->saddr)) ){
		if( (ipd = lookup_iphost(&ip->daddr)) ){
			const void *nhdr = (const unsigned char *)frame + hlen;
			const size_t nlen = be16toh(ip->tot_len) - hlen;

			switch(ip->protocol){
			case IPPROTO_TCP:{
				handle_tcp_packet(i,nhdr,nlen);
				break;
			}case IPPROTO_UDP:{
				handle_udp_packet(i,nhdr,nlen);
				break;
			}case IPPROTO_ICMP:{
				handle_icmp_packet(i,nhdr,nlen);
				break;
			}case IPPROTO_IGMP:{
				handle_igmp_packet(i,nhdr,nlen);
				break;
			}default:{
				printf("%s noproto for %u\n",__func__,ip->protocol);
				++i->noprotocol;
				break;
			}
			}
		}
	}
	// FIXME...
}
