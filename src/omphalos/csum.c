#include <stdio.h>
#include <linux/ip.h>
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
