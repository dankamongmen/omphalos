#include <assert.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/tx.h>
#include <omphalos/udp.h>
#include <omphalos/csum.h>
#include <omphalos/dhcp.h>
#include <omphalos/diag.h>
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

#define DHCP_MAGIC_COOKIE __constant_htonl(0x63825363lu)

struct dhcphdr {
	uint8_t mtype;
	uint8_t htype;
	uint8_t halen;
	uint8_t hops;
	uint32_t xid;
	uint16_t seconds;
	uint16_t bootpflags;
	uint32_t caddr;
	uint32_t yaddr;
	uint32_t saddr;
	uint32_t raddr;
	unsigned char haddr[16];
	unsigned char padding[192];
	uint32_t cookie;
} __attribute__ ((packed));

struct dhcp6hdr {
	uint8_t mtype;
	union {
		unsigned c: 1;
		unsigned reserved: 15;
	} c;
	uint128_t lladdr;
	uint128_t relayaddr;
} __attribute__ ((packed));

enum {
	BOOTP_MTYPE_REQUEST = 1,
};

enum {
	BOOTP_HTYPE_ETHERNET = 1,
};

enum {
	DHCP6_MTYPE_SOLICIT = 1,
};

// Minimum dhcp option. Usually a value will follow, described by "len".
struct dhcpop {
	uint8_t op;
	uint8_t len;
} __attribute__ ((packed));

struct dhcpopstruct {
	struct dhcpop mtypeop;
	uint8_t mtype;
	struct dhcpop cliidop;
	uint8_t htype;
	unsigned char haddr[ETH_ALEN];
	struct dhcpop ipreqop;
	uint32_t ipreq;
	struct dhcpop paramop;
	uint32_t params;
	uint8_t endop;
	uint8_t padding[7];
} __attribute__ ((packed));

enum {
	DHCP_OP_ADDRESS_REQ = 0x32u,
	DHCP_OP_MESSAGE_TYPE = 0x35u,
	DHCP_OP_PARAM_LIST = 0x37u,
	DHCP_OP_CLIENT_ID = 0x3du,
};

enum {
	DHCP_MESSAGE_SOLICIT = 1u,
};

enum {
	DHCP_HTYPE_ETHERNET = 1u,
};

#define DHCP4_REQ_LEN (sizeof(struct dhcphdr) + sizeof(struct dhcpopstruct))
int dhcp4_probe(interface *i,const uint32_t *saddr){
	struct dhcpopstruct *dhcpop;
	struct tpacket_hdr *hdr;
	struct dhcphdr *dhcp;
	struct udphdr *udp;
	struct dhcpop *op;
	struct iphdr *ip;
	size_t fsize,off;
	void *frame;
	int r;

	if(i->addrlen > sizeof(dhcp->haddr) || i->addrlen != sizeof(dhcpop->haddr)){
		diagnostic("Interface hardware addrlen too large (%zu)",i->addrlen);
		return -1;
	}
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
	if((r = prep_udp4(udp,fsize,__constant_htons(BOOTP_UDP_PORT),__constant_htons(DHCP_UDP_PORT),__constant_htons(DHCP4_REQ_LEN))) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	dhcp = (struct dhcphdr *)((char *)frame + off);
	memset(dhcp,0,sizeof(*dhcp));
	dhcp->mtype = BOOTP_MTYPE_REQUEST;
	dhcp->htype = BOOTP_HTYPE_ETHERNET;
	dhcp->halen = i->addrlen;
	dhcp->xid = random(); // FIXME?
	dhcp->cookie = DHCP_MAGIC_COOKIE;
	dhcp->yaddr = *saddr;
	memcpy(dhcp->haddr,i->addr,i->addrlen);
	off += sizeof(struct dhcphdr);
	fsize -= sizeof(struct dhcphdr);
	dhcpop = (struct dhcpopstruct *)((char *)frame + off);
	memset(dhcpop,0,sizeof(*dhcpop)); // FIXME
	op = (struct dhcpop *)dhcpop;
	op->op = DHCP_OP_MESSAGE_TYPE;
	op->len = 1u;
	dhcpop->mtype = DHCP_MESSAGE_SOLICIT;
	op = (struct dhcpop *)((char *)op + op->len + sizeof(*op));
	op->op = DHCP_OP_CLIENT_ID;
	op->len = 7u;
	dhcpop->htype = DHCP_HTYPE_ETHERNET;
	memcpy(dhcpop->haddr,i->addr,sizeof(dhcpop->haddr));
	op = (struct dhcpop *)((char *)op + op->len + sizeof(*op));
	op->op = DHCP_OP_ADDRESS_REQ;
	op->len = 4u;
	dhcpop->endop = 0xffu;
	op = (struct dhcpop *)((char *)op + op->len + sizeof(*op));
	op->op = DHCP_OP_PARAM_LIST;
	op->len = 4u;
	dhcpop->params = __constant_htonl(0x0103062a);
	off += sizeof(struct dhcpopstruct);
	fsize -= sizeof(struct dhcpopstruct);
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

#define DHCP6_REQ_LEN sizeof(struct dhcp6hdr)
int dhcp6_probe(interface *i,const uint128_t saddr){
	uint128_t daddr = DHCPV6_RELAYSSERVERS;
	struct tpacket_hdr *hdr;
	struct dhcp6hdr *dhcp;
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
	if((r = prep_udp6(udp,fsize,__constant_htons(DHCP6CLI_UDP_PORT),__constant_htons(DHCP6SRV_UDP_PORT),__constant_htons(DHCP6_REQ_LEN))) < 0){
		goto err;
	}
	fsize -= r;
	off += r;
	dhcp = (struct dhcp6hdr *)((char *)frame + off);
	dhcp->mtype = DHCP6_MTYPE_SOLICIT;
	dhcp->c.c = 1u;
	off += DHCP6_REQ_LEN;
	ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(off - hdr->tp_mac - sizeof(struct ethhdr) - sizeof(*ip));
	udp->check = udp6_csum(udp);
	hdr->tp_len = off - hdr->tp_mac;
	send_tx_frame(i,frame);
	return 0;

err:
	abort_tx_frame(i,frame);
	return -1;
}
