#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <omphalos/udp.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <asm/byteorder.h>
#include <omphalos/route.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_mdns_packet(const omphalos_iface *iface,omphalos_packet *op,
			const void *frame,size_t len){
	handle_dns_packet(iface,op,frame,len);
}

int tx_mdns_ptr(const omphalos_iface *octx,interface *i,const char *str,
		int fam,const void *lookup){
	uint128_t mcast_netaddr;
	const void *mcast_addr;
	struct routepath rp;
	size_t flen;
	void *frame;

	if(!(i->flags & IFF_MULTICAST)){
		return 0;
	}
	if(!get_source_address(octx,i,fam,lookup,&rp.src)){
		return 0;
	}
	rp.i = i;
	if(fam == AF_INET){
		mcast_addr = "\x01\x00\x5e\x00\x00\xfb";
		if((rp.l2 = lookup_l2host(octx,i,mcast_addr)) == NULL){
			return -1;
		}
		mcast_netaddr[0] = __constant_htonl(0xe00000fbu);
		if((rp.l3 = lookup_l3host(octx,i,rp.l2,AF_INET,&mcast_netaddr)) == NULL){
			return -1;
		}
		if((frame = get_tx_frame(octx,i,&flen)) == NULL){
			return -1;
		}
		if(setup_dns_ptr(&rp,AF_INET,MDNS_UDP_PORT,flen,frame,str)){
			abort_tx_frame(octx,i,frame);
			return -1;
		}
		send_tx_frame(octx,i,frame);
	}else if(fam == AF_INET6){
		mcast_addr = "\x33\x33\x00\x00\x00\xfb";
		if((rp.l2 = lookup_l2host(octx,i,mcast_addr)) == NULL){
			return -1;
		}
		mcast_netaddr[0] = __constant_htonl(0xff020000u);
		mcast_netaddr[1] = __constant_htonl(0x00000000u);
		mcast_netaddr[2] = __constant_htonl(0x00000000u);
		mcast_netaddr[3] = __constant_htonl(0x000000fbu);
		if((rp.l3 = lookup_l3host(octx,i,rp.l2,AF_INET6,&mcast_netaddr)) == NULL){
			return -1;
		}
		if((frame = get_tx_frame(octx,i,&flen)) == NULL){
			return -1;
		}
		if(setup_dns_ptr(&rp,AF_INET6,MDNS_UDP_PORT,flen,frame,str)){
			abort_tx_frame(octx,i,frame);
			return -1;
		}
		send_tx_frame(octx,i,frame);
	}
	return 0;
}
