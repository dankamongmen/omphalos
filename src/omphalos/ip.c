#include <endian.h>
#include <linux/ip.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <omphalos/ip.h>
#include <omphalos/interface.h>

static void
handle_ipv4_packet(interface *i,const struct iphdr *ip,const void *frame,size_t len){
	unsigned hlen = ip->ihl << 2u;

	if(len < hlen){
		++i->malformed;
		return;
	}
	if(len != be16toh(ip->tot_len)){
		++i->malformed;
		return;
	}
	frame = NULL; // FIXME
}

static void
handle_ipv6_packet(interface *i,const struct ipv6hdr *ip,const void *frame,size_t len){
	if(len < ip->nexthdr){
		++i->malformed;
		return;
	}
	frame = NULL; // FIXME
}

void handle_ip_packet(interface *i,const void *frame,size_t len){
	const struct iphdr *ip = frame;

	if(len < sizeof(*ip)){
		++i->malformed;
		return;
	}
	switch(ip->version){
		case 4:{
			handle_ipv4_packet(i,ip,frame,len);
			break;
		}case 6:{
			handle_ipv6_packet(i,(const struct ipv6hdr *)frame,frame,len);
			break;
		}default:{
			++i->noprotocol;
			break;
		}
	}
}
