#include <assert.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/udp.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <omphalos/csum.h>
#include <asm/byteorder.h>
#include <omphalos/diag.h>
#include <omphalos/route.h>
#include <omphalos/service.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_mdns_packet(omphalos_packet *op,const void *frame,size_t len){
	if(handle_dns_packet(op,frame,len) == 1){
		observe_service(op->i,op->l2s,op->l3s,op->l3proto,
				op->l4src,L"mDNS",NULL);
	}
}

int tx_mdns_ptr(interface *i,const char *str,int fam,const void *lookup){
	uint128_t mcast_netaddr;
	const void *mcast_addr;
	struct routepath rp;
	int ret = -1;
	size_t flen;
	void *frame;

	if(!(i->flags & IFF_MULTICAST)){
		return 0;
	}
	rp.i = i;
	if(get_source_address(i,AF_INET,fam == AF_INET ? lookup : NULL,&rp.src)){
		mcast_addr = "\x01\x00\x5e\x00\x00\xfb";
		if((rp.l2 = lookup_l2host(i,mcast_addr)) == NULL){
			return -1;
		}
		mcast_netaddr[0] = __constant_htonl(0xe00000fbu);
		if( (rp.l3 = lookup_local_l3host(i,rp.l2,AF_INET,&mcast_netaddr)) ){
			if( (frame = get_tx_frame(i,&flen)) ){
				if(setup_dns_ptr(&rp,AF_INET,MDNS_UDP_PORT,flen,frame,str)){
					abort_tx_frame(i,frame);
				}else{
					send_tx_frame(i,frame);
					ret = 0;
				}
			}
		}
	}
	if(get_source_address(i,AF_INET6,fam == AF_INET6 ? lookup : NULL,&rp.src)){
		mcast_addr = "\x33\x33\x00\x00\x00\xfb";
		if((rp.l2 = lookup_l2host(i,mcast_addr)) == NULL){
			return -1;
		}
		mcast_netaddr[0] = __constant_htonl(0xff020000u);
		mcast_netaddr[1] = __constant_htonl(0x00000000u);
		mcast_netaddr[2] = __constant_htonl(0x00000000u);
		mcast_netaddr[3] = __constant_htonl(0x000000fbu);
		if((rp.l3 = lookup_local_l3host(i,rp.l2,AF_INET6,&mcast_netaddr)) == NULL){
			return -1;
		}
		if((frame = get_tx_frame(i,&flen)) == NULL){
			return -1;
		}
		if(setup_dns_ptr(&rp,AF_INET6,MDNS_UDP_PORT,flen,frame,str)){
			abort_tx_frame(i,frame);
			return -1;
		}
		send_tx_frame(i,frame);
	}
	return ret;
}

static int
tx_sd4_enumerate(interface *i){
	const struct ip4route *i4;
	int ret = 0;

	for(i4 = i->ip4r ; i4 ; i4 = i4->next){
		const unsigned char hw[ETH_ALEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb };
                uint32_t net = htonl(0xd00000fa);
                struct tpacket_hdr *thdr;
                struct udphdr *udp;
                struct iphdr *ip;
                size_t flen,tlen;
                void *frame;
                int r;

                if(!(i4->addrs & ROUTE_HAS_SRC)){
                        continue; // not cause for an error
                }
                if((frame = get_tx_frame(i,&flen)) == NULL){
                        ret = -1;
                        continue;
                }
                thdr = frame;
                tlen = thdr->tp_mac;
                if((r = prep_eth_header((char *)frame + tlen,flen - tlen,i,
                                                hw,ETH_P_IP)) < 0){
                        abort_tx_frame(i,frame);
                        ret = -1;
                        continue;
                }
                tlen += r;
                ip = (struct iphdr *)((char *)frame + tlen);
                if((r = prep_ipv4_header(ip,flen - tlen,i4->src,net,IPPROTO_UDP)) < 0){
                        abort_tx_frame(i,frame);
                        ret = -1;
                        continue;
                }
                tlen += r;
                if(flen - tlen < sizeof(*udp)){
                        abort_tx_frame(i,frame);
                        ret = -1;
                        continue;
                }
                udp = (struct udphdr *)((char *)frame + tlen);
                udp->source = htons(MDNS_UDP_PORT);
                udp->dest = htons(MDNS_UDP_PORT);
                tlen += sizeof(*udp);
                thdr->tp_len = tlen;
                ip->tot_len = htons(thdr->tp_len - ((const char *)ip - (const char *)frame));
		udp->len = ntohs(ip->tot_len) - sizeof(*udp);
                udp->check = udp4_csum(ip);
                send_tx_frame(i,frame); // FIXME get return value...
	}
	return ret;
}

static int
tx_sd6_enumerate(interface *i){
	const struct ip6route *i6;
	int ret = 0;

	for(i6 = i->ip6r ; i6 ; i6 = i6->next){
		const unsigned char hw[ETH_ALEN] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01 };
                uint128_t net = { htonl(0xff020000ul), htonl(0x0ul),
                                        htonl(0x0ul), htonl(0x1ul) };
                struct tpacket_hdr *thdr;
                struct udphdr *udp;
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
                if((r = prep_ipv6_header(ip,flen - tlen,i6->src,net,IPPROTO_UDP)) < 0){
                        abort_tx_frame(i,frame);
                        ret = -1;
                        continue;
                }
                tlen += r;
                if(flen - tlen < sizeof(*udp)){
                        abort_tx_frame(i,frame);
                        ret = -1;
                        continue;
                }
                udp = (struct udphdr *)((char *)frame + tlen);
                udp->source = htons(MDNS_UDP_PORT);
                udp->dest = htons(MDNS_UDP_PORT);
                tlen += sizeof(*udp);
                thdr->tp_len = tlen;
                ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(thdr->tp_len -
                        ((const char *)udp - (const char *)frame));
		udp->len = ip->ip6_ctlun.ip6_un1.ip6_un1_plen;
                udp->check = udp6_csum(ip);
                send_tx_frame(i,frame); // FIXME get return value...
	}
	return ret;
}

int mdns_sd_enumerate(interface *i){
	int ret = 0;

	if(!(i->flags & IFF_MULTICAST)){
		return 0;
	}
	ret |= tx_sd4_enumerate(i);
	ret |= tx_sd6_enumerate(i);
	return ret;
}
