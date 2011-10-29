#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <linux/ip.h>

static uint16_t
ipv4_csum(const void *hdr){
	size_t len = ((const struct iphdr *)hdr)->ihl << 2u;
	const uint16_t *cur;
	uint32_t sum;
	unsigned z;

	sum = 0;
	cur = hdr;
	for(z = 0 ; z < len / sizeof(*cur) ; ++z){
		sum += cur[z];
	}
	fprintf(stderr,"SUM: %u (0x%08x)\n",sum,sum);
	while(sum >> 16){
		sum = (sum & 0xffffu) + (sum >> 16);
	}
	return ~sum;
}


int main(void){
	const char a[] = { 0x45, 0x00, 0x00, 0x46, 0x51, 0xdc, 0x00, 0x00,
				0x40, 0x11, 0x2b, 0xc9, 0x7f, 0x00, 0x00, 0x01,
				0x7f, 0x00, 0x00, 0x01, };
	const char b[] = { 0x45, 0x00, 0x00, 0x48, 0x67, 0x45, 0x00, 0x00,
				0x40, 0x11, 0x52, 0xba, 0x00, 0x00, 0x00, 0x00,
				0xc0, 0xa8, 0x01, 0xfe, };
	uint16_t cs;

	cs = ipv4_csum(a);
	fprintf(stderr,"csum: 0x%04hx\n",cs);
	cs = ipv4_csum(b);
	fprintf(stderr,"csum: 0x%04hx\n",cs);
	return EXIT_SUCCESS;
}
