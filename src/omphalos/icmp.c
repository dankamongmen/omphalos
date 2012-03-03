#include <linux/ip.h>
#include <linux/icmp.h>
#include <netinet/ip6.h>
#include <omphalos/tx.h>
#include <omphalos/ip.h>
#include <omphalos/nd.h>
#include <omphalos/mld.h>
#include <netinet/icmp6.h>
#include <omphalos/csum.h>
#include <omphalos/icmp.h>
#include <omphalos/diag.h>
#include <linux/if_packet.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

#define ICMPV6_MLD2_REPORT	143

#define COMMON_ICMPV4_LEN 4
void handle_icmp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmphdr *icmp = frame;
	/*const void *dframe;
	size_t dlen;*/

	// Check length for the mandatory minimum ICMPv4 size...
	if(len < sizeof(*icmp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// ...but only skip COMMON_ICMPV4_LEN.
	/*dframe = (const char *)frame + COMMON_ICMPV4_LEN;
	dlen = len - COMMON_ICMPV4_LEN;*/
	switch(icmp->type){
		case ICMP_ECHOREPLY:
			break;
		case ICMP_DEST_UNREACH:
			break;
		case ICMP_SOURCE_QUENCH:
			break;
		case ICMP_REDIRECT:
			break;
		case ICMP_ECHO:
			break;
		case ICMP_TIME_EXCEEDED:
			break;
		case ICMP_PARAMETERPROB:
			break;
		case ICMP_TIMESTAMP:
			break;
		case ICMP_TIMESTAMPREPLY:
			break;
		case ICMP_INFO_REQUEST:
			break;
		case ICMP_INFO_REPLY:
			break;
		case ICMP_ADDRESS:
			break;
		case ICMP_ADDRESSREPLY:
			break;
		default:
			diagnostic("Unknown ICMPv4 type: %u",icmp->type);
			op->noproto = 1;
			break;
	}
}
#undef COMMON_ICMPV4_LEN

// The icmp_hdr structure includes the second 32-bit word from the header,
// though this is different among different ICMPv6 types. Pass it along.
#define COMMON_ICMPV6_LEN 4
void handle_icmp6_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct icmp6_hdr *icmp = frame;
	const void *dframe;
	size_t dlen;

	// Check length for the mandatory minimum ICMPv6 size...
	if(len < sizeof(*icmp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	// ...but only skip COMMON_ICMPV6_LEN.
	dframe = (const char *)frame + COMMON_ICMPV6_LEN;
	dlen = len - COMMON_ICMPV6_LEN;
	// Types 1--127 are errors; 128--255 are informational
	switch(icmp->icmp6_type){
		case ICMP6_DST_UNREACH: // FIXME
			break;
		case ICMP6_PACKET_TOO_BIG: // FIXME
			break;
		case ICMP6_TIME_EXCEEDED: // FIXME
			break;
		case ICMP6_PARAM_PROB: // FIXME
			break;
		case ICMP6_ECHO_REQUEST: // FIXME
			break;
		case ICMP6_ECHO_REPLY: // FIXME
			break;
		case MLD_LISTENER_QUERY:
			break;
		case MLD_LISTENER_REPORT:
			break;
		case MLD_LISTENER_REDUCTION:
			break;
		case ND_ROUTER_SOLICIT:
			handle_nd_routersol(op,dframe,dlen);
			break;
		case ND_ROUTER_ADVERT:
			handle_nd_routerad(op,dframe,dlen);
			break;
		case ND_NEIGHBOR_SOLICIT:
			handle_nd_neighsol(op,dframe,dlen);
			break;
		case ND_NEIGHBOR_ADVERT:
			handle_nd_neighad(op,dframe,dlen);
			break;
		case ND_REDIRECT:
			handle_nd_redirect(op,dframe,dlen);
			break;
		case ICMPV6_MLD2_REPORT:
			handle_mld_packet(op,dframe,dlen);
			break;
		default:
			diagnostic("Unknown ICMPv6 type: %u",icmp->icmp6_type);
			op->noproto = 1;
			break;
	}
}
#undef COMMON_ICMPV6_LEN

// We have to give some minimum payload, or else we'll draw an EINVAL.
#define PING4_PAYLOAD_LEN 20

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
int tx_ipv4_bcast_pings(interface *i,const uint32_t *saddr){
	struct tpacket_hdr *thdr;
	struct icmphdr *icmp;
	struct iphdr *ip;
	size_t flen,tlen;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&flen)) == NULL){
		return -1;
	}
	thdr = frame;
	tlen = thdr->tp_mac;
	if((r = prep_eth_bcast((char *)frame + tlen,flen - tlen,i,ETH_P_IP)) < 0){
		abort_tx_frame(i,frame);
		return -1;
	}
	tlen += r;
	ip = (struct iphdr *)((char *)frame + tlen);
	// FIXME get bcast address appropriate for route
	if((r = prep_ipv4_bcast(ip,flen - tlen,*saddr,IPPROTO_ICMP)) < 0){
		abort_tx_frame(i,frame);
		return -1;
	}
	tlen += r;
	if(flen - tlen < sizeof(*icmp) + PING4_PAYLOAD_LEN){
		abort_tx_frame(i,frame);
		return -1;
	}
	icmp = (struct icmphdr *)((char *)frame + tlen);
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	tlen += sizeof(*icmp) + PING4_PAYLOAD_LEN;
	thdr->tp_len = tlen;
	ip->tot_len = htons((const char *)icmp - (const char *)ip + sizeof(*icmp) + PING4_PAYLOAD_LEN);
	icmp->checksum = 0;
	icmp->checksum = icmp4_csum(icmp,sizeof(*icmp));
	ip->check = ipv4_csum(ip);
	return send_tx_frame(i,frame);
}

// Always goes to ff02::2 (ALL-HOSTS), from each source address.
int tx_ipv6_bcast_pings(interface *i,const uint128_t saddr){
	const unsigned char hw[ETH_ALEN] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01 };
	uint128_t net = { htonl(0xff020000ul), htonl(0x0ul),
				htonl(0x0ul), htonl(0x1ul) };
	struct tpacket_hdr *thdr;
	struct icmp6_hdr *icmp;
	struct ip6_hdr *ip;
	size_t flen,tlen;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&flen)) == NULL){
		return -1;
	}
	thdr = frame;
	tlen = thdr->tp_mac;
	if((r = prep_eth_header((char *)frame + tlen,flen - tlen,i,
					hw,ETH_P_IPV6)) < 0){
		abort_tx_frame(i,frame);
		return -1;
	}
	tlen += r;
	ip = (struct ip6_hdr *)((char *)frame + tlen);
	if((r = prep_ipv6_header(ip,flen - tlen,saddr,net,IPPROTO_ICMP6)) < 0){
		abort_tx_frame(i,frame);
		return -1;
	}
	tlen += r;
	if(flen - tlen < sizeof(*icmp)){
		abort_tx_frame(i,frame);
		return -1;
	}
	icmp = (struct icmp6_hdr *)((char *)frame + tlen);
	icmp->icmp6_type = ICMP6_ECHO_REQUEST;
	icmp->icmp6_code = 0;
	tlen += sizeof(*icmp);
	thdr->tp_len = tlen;
	ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(thdr->tp_len -
		((const char *)icmp - (const char *)frame));
	icmp->icmp6_cksum = icmp6_csum(ip);
	return send_tx_frame(i,frame);
}
