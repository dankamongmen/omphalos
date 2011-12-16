#include <string.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/tx.h>
#include <omphalos/ip.h>
#include <omphalos/udp.h>
#include <omphalos/csum.h>
#include <omphalos/diag.h>
#include <omphalos/ssdp.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define SSDP_METHOD_NOTIFY "NOTIFY "
#define SSDP_METHOD_SEARCH "SEARCH "

#define SSDP_NET4 0xeffffffalu

// Returns 1 for a valid SSDP response, -1 for a valid SSDP query, 0 otherwise
int handle_ssdp_packet(omphalos_packet *op,const void *frame,size_t len){
	if(len < __builtin_strlen(SSDP_METHOD_NOTIFY)){
		diagnostic("%s frame too short (%zu)",__func__,len);
		op->malformed = 1;
		return 0;
	}
	if(strncmp(frame,SSDP_METHOD_NOTIFY,__builtin_strlen(SSDP_METHOD_NOTIFY)) == 0){
		return 1;
	}else if(strncmp(frame,SSDP_METHOD_SEARCH,__builtin_strlen(SSDP_METHOD_SEARCH)) == 0){
		return -1;
	}
	return 0;
}

static const char m4_search[] = "M-SEARCH * HTTP/1.1\r\n"
"HOST: 239.255.255.250:1900\r\n"
"MAN: \"ssdp:discover\"\r\n"
"MX: 5\r\n"
"ST: ssdp:all\r\n";

static int
setup_ssdp_query(void *frame,size_t flen){
	if(flen < sizeof(m4_search) - 1){
		return -1;
	}
	memcpy(frame,m4_search,sizeof(m4_search) - 1);
	return sizeof(m4_search) - 1;
}

static int
ssdp_ip4_msearch(interface *i,const uint32_t *saddr){
	const struct ip4route *i4;
	int ret = 0;

	for(i4 = i->ip4r ; i4 ; i4 = i4->next){
		const unsigned char hw[ETH_ALEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xc };
                uint32_t net = htonl(SSDP_NET4);
                struct tpacket_hdr *thdr;
                struct udphdr *udp;
                struct iphdr *ip;
                size_t flen,tlen;
                void *frame;
                int r;

                if(!(i4->addrs & ROUTE_HAS_SRC)){
                        continue; // not cause for an error
                }
		if(saddr && i4->src != *saddr){
			continue;
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
                udp->source = htons(SSDP_UDP_PORT);
                udp->dest = htons(SSDP_UDP_PORT);
                tlen += sizeof(*udp);
		r = setup_ssdp_query((char *)frame + tlen,flen - tlen);
		if(r < 0){
			abort_tx_frame(i,frame);
			ret = -1;
			continue;
		}
		tlen += r;
                ip->tot_len = htons(tlen - ((const char *)ip - (const char *)frame));
		ip->check = ipv4_csum(ip);
		udp->len = htons(ntohs(ip->tot_len) - ip->ihl * 4u);
                udp->check = udp4_csum(ip);
                thdr->tp_len = tlen - thdr->tp_mac;
                ret |= send_tx_frame(i,frame);
		if(saddr){
			break; // we're done
		}
	}
	return ret;
}

static int
ssdp_ip6_msearch(interface *i,const uint128_t saddr){
	const struct ip6route *i6;
	int ret = 0;

	for(i6 = i->ip6r ; i6 ; i6 = i6->next){
		const unsigned char hw[ETH_ALEN] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0xc };
                uint128_t net = { htonl(0xff020000ul), htonl(0x0ul),
                                        htonl(0x0ul), htonl(0xcul) };
                struct tpacket_hdr *thdr;
                struct udphdr *udp;
                struct ip6_hdr *ip;
                size_t flen,tlen;
                void *frame;
                int r;

                if(!(i6->addrs & ROUTE_HAS_SRC)){
                        continue; // not cause for an error
                }
		if(saddr && !equal128(i6->src,saddr)){
			continue;
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
                udp->source = htons(SSDP_UDP_PORT);
                udp->dest = htons(SSDP_UDP_PORT);
                tlen += sizeof(*udp);
		r = setup_ssdp_query((char *)frame + tlen,flen - tlen);
		if(r < 0){
			abort_tx_frame(i,frame);
			ret = -1;
			continue;
		}
		tlen += r;
                thdr->tp_len = tlen;
                ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(thdr->tp_len -
                        ((const char *)udp - (const char *)frame));
		udp->len = ip->ip6_ctlun.ip6_un1.ip6_un1_plen;
                udp->check = udp6_csum(ip);
                thdr->tp_len -= thdr->tp_mac;
                ret |= send_tx_frame(i,frame);
		if(saddr){
			break; // we're done
		}
	}
	return ret;
}

int ssdp_msearch(int fam,interface *i,const void *saddr){
	switch(fam){
		case AF_INET: return ssdp_ip4_msearch(i,saddr);
		case AF_INET6: return ssdp_ip6_msearch(i,saddr);
		default: return -1;
	}
}
