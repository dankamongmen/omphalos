#include <zlib.h>
#include <stdio.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <omphalos/icmp.h>
#include <omphalos/csum.h>

uint16_t ipv4_csum(const void *hdr){
	size_t len = ((const struct iphdr *)hdr)->ihl << 2u;
	const uint16_t *cur;
	uint32_t sum;
	unsigned z;

	sum = 0;
	cur = hdr;
	for(z = 0 ; z < len / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	while(sum >> 16){
		sum = (sum & 0xffff) + (sum >> 16);
	}
	return ~sum;
}

// hdr must be a valid ICMPv4 header
uint16_t icmp4_csum(const void *hdr,size_t dlen){
	const struct icmphdr *ih = hdr;
	const uint16_t *cur;
	uint32_t sum,fold;
	unsigned z;

	sum = 0;
	// ICMPv4 checksum is over ICMP header and ICMP data (zero padded to
	// make it a multiple of 16 bits). No pseudoheader.
	cur = (const uint16_t *)ih;
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)ih)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}

// hdr must be a valid ipv4 header
uint16_t udp4_csum(const void *hdr){
	const struct iphdr *ih = hdr;
	const struct udphdr *uh = (const void *)((const char *)hdr + (ih->ihl << 2u));
	uint16_t dlen = ntohs(uh->len);
	const uint16_t *cur;
	uint32_t sum,fold;
	unsigned z;

	sum = 0;
	// UDP4 checksum is over UDP header, UDP data (zero padded to make it a
	// multiple of 16 bits), and 12-byte IPv4 pseudoheader containing
	// source addr, dest addr, 8 bits of 0, protocol, and total UDP length.
	cur = (const uint16_t *)&ih->saddr;
	sum += cur[0];
	sum += cur[1];
	cur = (const uint16_t *)&ih->daddr;
	sum += cur[0];
	sum += cur[1];
	sum += htons(ih->protocol); // zeroes and protocol

	sum += htons(dlen); // total length
	cur = (const uint16_t *)uh; // now checksum over UDP header + data
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)uh)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}

// hdr must be a valid ipv6 header
uint16_t udp6_csum(const void *hdr){
	const struct ip6_hdr *ih = hdr;
	// FIXME doesn't work for more than one IPv6 header!
	const struct udphdr *uh = (const struct udphdr *)((const unsigned char *)hdr + 40);
	uint16_t dlen = ntohs(uh->len);
	const uint16_t *cur;
	uint16_t fold;
	uint32_t sum;
	unsigned z;

	sum = 0;
	// UDP6 checksum is over UDP header, UDP data (zero padded to make it a
	// multiple of 16 bits), and 40-byte IPv6 pseudoheader containing
	// source addr, dest addr, UDP length, 24 bits of 0 and next header
	sum += ih->ip6_src.s6_addr16[0];
	sum += ih->ip6_src.s6_addr16[1];
	sum += ih->ip6_src.s6_addr16[2];
	sum += ih->ip6_src.s6_addr16[3];
	sum += ih->ip6_src.s6_addr16[4];
	sum += ih->ip6_src.s6_addr16[5];
	sum += ih->ip6_src.s6_addr16[6];
	sum += ih->ip6_src.s6_addr16[7]; // saddr
	sum += ih->ip6_dst.s6_addr16[0];
	sum += ih->ip6_dst.s6_addr16[1];
	sum += ih->ip6_dst.s6_addr16[2];
	sum += ih->ip6_dst.s6_addr16[3];
	sum += ih->ip6_dst.s6_addr16[4];
	sum += ih->ip6_dst.s6_addr16[5];
	sum += ih->ip6_dst.s6_addr16[6];
	sum += ih->ip6_dst.s6_addr16[7]; // daddr
	sum += ih->ip6_ctlun.ip6_un1.ip6_un1_plen;
	sum += htons(17); // zeroes and protocol
	cur = (const uint16_t *)uh; // now checksum over UDP header + data
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)uh)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}

uint16_t icmp6_csum(const void *hdr){
	const struct ip6_hdr *ih = hdr;
	// FIXME doesn't work for more than one IPv6 header!
	const struct icmp6_hdr *ch = (const struct icmp6_hdr *)((const unsigned char *)hdr + 40);
	uint16_t dlen = ntohs(ih->ip6_ctlun.ip6_un1.ip6_un1_plen) - sizeof(*ih);
	const uint16_t *cur;
	uint16_t fold;
	uint32_t sum;
	unsigned z;

	sum = 0;
	// ICMPv6 checksum works just like UDPv6
	sum += ih->ip6_src.s6_addr16[0];
	sum += ih->ip6_src.s6_addr16[1];
	sum += ih->ip6_src.s6_addr16[2];
	sum += ih->ip6_src.s6_addr16[3];
	sum += ih->ip6_src.s6_addr16[4];
	sum += ih->ip6_src.s6_addr16[5];
	sum += ih->ip6_src.s6_addr16[6];
	sum += ih->ip6_src.s6_addr16[7]; // saddr
	sum += ih->ip6_dst.s6_addr16[0];
	sum += ih->ip6_dst.s6_addr16[1];
	sum += ih->ip6_dst.s6_addr16[2];
	sum += ih->ip6_dst.s6_addr16[3];
	sum += ih->ip6_dst.s6_addr16[4];
	sum += ih->ip6_dst.s6_addr16[5];
	sum += ih->ip6_dst.s6_addr16[6];
	sum += ih->ip6_dst.s6_addr16[7]; // daddr
	sum += ih->ip6_ctlun.ip6_un1.ip6_un1_plen;
	sum += htons(IPPROTO_ICMP6); // zeroes and protocol
	cur = (const uint16_t *)ch; // now checksum over ICMP header + data
	for(z = 0 ; z < dlen / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	if(dlen % 2){
		sum += ((uint16_t)(((const unsigned char *)ch)[dlen - 1]));
	}
	fold = 0;
	for(z = 0 ; z < sizeof(sum) / 2 ; ++z){
		fold += sum & 0xffffu;
		sum >>= 16u;
	}
	fold = ~(fold & 0xffffu);
	if(fold == 0u){
		return 0xffffu;
	}
	return fold;
}

uint32_t ieee80211_fcs(const void *frame,size_t len){
	return crc32(crc32(0L,Z_NULL,0),frame,len);
}
