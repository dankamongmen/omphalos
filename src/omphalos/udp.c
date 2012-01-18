#include <linux/ip.h>
#include <sys/types.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/udp.h>
#include <omphalos/dns.h>
#include <omphalos/diag.h>
#include <omphalos/dhcp.h>
#include <omphalos/mdns.h>
#include <omphalos/ssdp.h>
#include <asm/byteorder.h>
#include <omphalos/netbios.h>
#include <omphalos/service.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME we want an automata-based approach to generically match. until then,
// use well-known ports, ugh...
void handle_udp_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct udphdr *udp = frame;
	const void *ubdy;
	uint16_t ulen;

	if(len < sizeof(*udp)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	ubdy = (const char *)udp + sizeof(*udp);
	ulen = len - sizeof(*udp);
	op->l4src = udp->source;
	op->l4dst = udp->dest;
	switch(udp->source){
		case __constant_htons(DNS_UDP_PORT):{
			if(handle_dns_packet(op,ubdy,ulen) == 1){
				observe_service(op->i,op->l2s,op->l3s,op->l3proto,
					op->l4src,L"DNS",NULL);
			}
		}break;
		case __constant_htons(SSDP_UDP_PORT):{
			if(handle_ssdp_packet(op,ubdy,ulen) == 1){
				observe_service(op->i,op->l2s,op->l3s,op->l3proto,
					op->l4src,L"UPnP",NULL);
			}
		}break;
		case __constant_htons(MDNS_UDP_PORT):{
			if(udp->dest == __constant_htons(MDNS_NATPMP1_UDP_PORT) ||
			   udp->dest == __constant_htons(MDNS_NATPMP2_UDP_PORT)){
				handle_natpmp_packet(op,ubdy,ulen);
			}else{
				handle_mdns_packet(op,ubdy,ulen);
			}
		}break;
		case __constant_htons(MDNS_NATPMP1_UDP_PORT):
		case __constant_htons(MDNS_NATPMP2_UDP_PORT):{
			handle_natpmp_packet(op,ubdy,ulen);
		}case __constant_htons(NETBIOS_NS_UDP_PORT):{
			if(udp->dest == __constant_htons(NETBIOS_NS_UDP_PORT)){
				handle_netbios_ns_packet(op,ubdy,ulen);
			}
		}break;
		case __constant_htons(DHCP_UDP_PORT):{
			// ensure IPv4? FIXME?
			if(udp->dest == __constant_htons(BOOTP_UDP_PORT)){
				if(handle_dhcp_packet(op,ubdy,ulen)){
					observe_service(op->i,op->l2s,op->l3s,op->l3proto,
						op->l4src,L"DHCP",NULL);
				}
			}
		}break;
		case __constant_htons(DHCP6SRV_UDP_PORT):{
			// ensure IPv6? FIXME?
			if(udp->dest == __constant_htons(DHCP6CLI_UDP_PORT)){
				if(handle_dhcp6_packet(op,ubdy,ulen)){
					observe_service(op->i,op->l2s,op->l3s,op->l3proto,
						op->l4src,L"DHCPv6",NULL);
				}
			}
		}break;
	}
}
