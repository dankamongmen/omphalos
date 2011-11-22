#include <linux/icmp.h>
#include <netinet/ip6.h>
#include <omphalos/tx.h>
#include <omphalos/ip.h>
#include <omphalos/nd.h>
#include <netinet/icmp6.h>
#include <omphalos/csum.h>
#include <omphalos/icmp.h>
#include <omphalos/diag.h>
#include <linux/if_packet.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

void handle_icmp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;

	if(len < sizeof(*icmp)){
		diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// FIXME
}

void handle_icmp6_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmp6_hdr *icmp = frame;
	const void *dframe;
	size_t dlen;

	if(len < sizeof(*icmp)){
		diagnostic(L"%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	dframe = (const char *)frame + sizeof(*icmp);
	dlen = len - sizeof(*icmp);
	switch(icmp->icmp6_type){
		case ND_ROUTER_ADVERT:
			handle_nd_routerad(op,dframe,dlen);
			break;
		case ND_NEIGHBOR_ADVERT:
			handle_nd_neighad(op,dframe,dlen);
			break;
		case ND_ROUTER_SOLICIT:
			handle_nd_routersol(op,dframe,dlen);
			break;
		case ND_NEIGHBOR_SOLICIT:
			handle_nd_neighsol(op,dframe,dlen);
			break;
		case ND_REDIRECT:
			handle_nd_redirect(op,dframe,dlen);
			break;
		default:
			diagnostic(L"Unknown ICMPv6 type: %u",icmp->icmp6_type);
			op->noproto = 1;
			break;
	}
}

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
static int
tx_ipv4_bcast_pings(interface *i){
	assert(i); // FIXME
	return -1;
}

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
static int
tx_ipv6_bcast_pings(interface *i){
	const struct ip6route *i6;
	int ret = 0;

	for(i6 = i->ip6r ; i6 ; i6 = i6->next){
		const unsigned char hw[ETH_ALEN] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01 };
		uint128_t net = { htonl(0xff020000ul), htonl(0x0ul),
					htonl(0x0ul), htonl(0x1ul) };
		struct tpacket_hdr *thdr;
		struct icmp6_hdr *icmp;
		struct ip6_hdr *ip;
		size_t flen,tlen;
		void *frame;
		int r;

		if(!(i6->addrs & ROUTE_HAS_SRC)){
			continue; // not cause for an error
		}
		if((frame = get_tx_frame(i,&flen)) == NULL){
			ret = -1;
			continue;
		}
		thdr = frame;
		tlen = thdr->tp_mac;
		if((r = prep_eth_header((char *)frame + tlen,flen - tlen,i,
						hw,ETH_P_IPV6)) < 0){
			abort_tx_frame(i,frame);
			ret = -1;
			continue;
		}
		tlen += r;
		ip = (struct ip6_hdr *)((char *)frame + tlen);
		if((r = prep_ipv6_header(ip,flen - tlen,i6->src,net,IPPROTO_ICMP6)) < 0){
			abort_tx_frame(i,frame);
			ret = -1;
			continue;
		}
		tlen += r;
		if(flen - tlen < sizeof(*icmp)){
			abort_tx_frame(i,frame);
			ret = -1;
			continue;
		}
		icmp = (struct icmp6_hdr *)((char *)frame + tlen);
		icmp->icmp6_type = ICMP6_ECHO_REQUEST;
		icmp->icmp6_code = 0;
		tlen += sizeof(*icmp);
		thdr->tp_len = tlen;
		ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(thdr->tp_len -
			((const char *)icmp - (const char *)frame));
		icmp->icmp6_cksum = icmp6_csum(ip);
		send_tx_frame(i,frame); // FIXME get return value...
	}
	return ret;
}

int tx_broadcast_pings(int fam,interface *i){
	if(fam == AF_INET){
		return tx_ipv4_bcast_pings(i);
	}else if(fam == AF_INET6){
		return tx_ipv6_bcast_pings(i);
	}
	return -1;
}
