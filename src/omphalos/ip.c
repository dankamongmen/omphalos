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
	// FIXME...
}
