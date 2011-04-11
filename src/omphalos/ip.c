#include <endian.h>
#include <linux/ip.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <omphalos/ip.h>
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
	if(len < ip->nexthdr){
		printf("%s malformed with %zu\n",__func__,len);
		++i->malformed;
		return;
	}
	// FIXME...
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
	if(len != be16toh(ip->tot_len)){
		printf("%s malformed with %zu vs %hu\n",__func__,len,be16toh(ip->tot_len));
		++i->malformed;
		return;
	}
	if( (ips = lookup_iphost(ip->src)) ){
		if( (ipd = lookup_iphost(ip->dst)) ){
			const void *nhdr = (const unsigned char *)frame + hlen;
			const size_t nlen = len - hlen;

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
			}default:{
				printf("%s noproto for %u\n",__func__,ip->protocol);
				++i->noproto;
				break;
			}
			}
		}
	}
	// FIXME...
}
