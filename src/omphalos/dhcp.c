#include <assert.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/tx.h>
#include <omphalos/udp.h>
#include <omphalos/csum.h>
#include <omphalos/dhcp.h>
#include <omphalos/omphalos.h>
#include <omphalos/ethernet.h>
#include <omphalos/interface.h>

int handle_dhcp_packet(omphalos_packet *op,const void *frame,size_t fsize){
	assert(op && frame && fsize);
	return 1; // FIXME
}

int handle_dhcp6_packet(omphalos_packet *op,const void *frame,size_t fsize){
	assert(op && frame && fsize);
	return 1; // FIXME
}

#define DHCP4_REQ_LEN 44

int dhcp4_probe(interface *i,const uint32_t *saddr){
	struct tpacket_hdr *hdr;
	struct udphdr *udp;
	struct iphdr *ip;
	size_t fsize,off;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&fsize)) == NULL){
		return -1;
	}
	hdr = frame;
	off = hdr->tp_mac;
	if((r = prep_eth_bcast((char *)frame + off,fsize,i,ETH_P_IP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	ip = (struct iphdr *)((char *)frame + off);
	if((r = prep_ipv4_bcast(ip,fsize,*saddr,IPPROTO_UDP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	udp = (struct udphdr *)((char *)frame + off);
	if((r = prep_udp4(udp,fsize,DHCP_UDP_PORT,BOOTP_UDP_PORT,DHCP4_REQ_LEN)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	// FIXME
	ip->tot_len = htons(off - hdr->tp_mac - sizeof(struct ethhdr));
	ip->check = ipv4_csum(ip);
	udp->check = udp4_csum(udp);
	hdr->tp_len = off - hdr->tp_mac;
	send_tx_frame(i,frame);
	return 0;

err:
	abort_tx_frame(i,frame);
	return -1;
}

#define DHCP6_REQ_LEN 8
int dhcp6_probe(interface *i,const uint128_t saddr){
	uint128_t daddr = DHCPV6_RELAYSSERVERS;
	struct tpacket_hdr *hdr;
	struct ip6_hdr *ip;
	struct udphdr *udp;
	size_t fsize,off;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&fsize)) == NULL){
		return -1;
	}
	hdr = frame;
	off = hdr->tp_mac;
	if((r = prep_eth_bcast((char *)frame + off,fsize,i,ETH_P_IPV6)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	ip = (struct ip6_hdr *)((char *)frame + off);
	if((r = prep_ipv6_header(ip,fsize,saddr,daddr,IPPROTO_UDP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	udp = (struct udphdr *)((char *)frame + off);
	if((r = prep_udp6(udp,fsize,DHCP6CLI_UDP_PORT,DHCP6SRV_UDP_PORT,DHCP6_REQ_LEN)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	// FIXME
	ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(off - hdr->tp_mac - sizeof(struct ethhdr));
	udp->check = udp6_csum(udp);
	hdr->tp_len = off - hdr->tp_mac;
	send_tx_frame(i,frame);
	return 0;

err:
	abort_tx_frame(i,frame);
	return -1;
}
