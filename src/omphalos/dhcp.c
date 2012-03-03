#include <assert.h>
#include <omphalos/ip.h>
#include <omphalos/tx.h>
#include <omphalos/udp.h>
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
	size_t fsize,off;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&fsize)) == NULL){
		return -1;
	}
	off = ((struct tpacket_hdr *)frame)->tp_mac;
	if((r = prep_eth_bcast((char *)frame + off,fsize,i,ETH_P_IP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	if((r = prep_ipv4_bcast((char *)frame + off,fsize,*saddr,IPPROTO_UDP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	if((r = prep_udp4((char *)frame + off,fsize,DHCP_UDP_PORT,BOOTP_UDP_PORT,DHCP4_REQ_LEN)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	// FIXME
	send_tx_frame(i,frame);
	return 0;

err:
	abort_tx_frame(i,frame);
	return -1;
}

#define DHCP6_REQ_LEN 8
int dhcp6_probe(interface *i,const uint128_t saddr){
	uint128_t daddr = DHCPV6_RELAYSSERVERS;
	size_t fsize,off;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&fsize)) == NULL){
		return -1;
	}
	off = ((struct tpacket_hdr *)frame)->tp_mac;
	if((r = prep_eth_bcast((char *)frame + off,fsize,i,ETH_P_IP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	if((r = prep_ipv6_header((char *)frame + off,fsize,saddr,daddr,IPPROTO_UDP)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	if((r = prep_udp6((char *)frame + off,fsize,DHCP6CLI_UDP_PORT,DHCP6SRV_UDP_PORT,DHCP6_REQ_LEN)) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	// FIXME
	send_tx_frame(i,frame);
	return 0;

err:
	abort_tx_frame(i,frame);
	return -1;
}
