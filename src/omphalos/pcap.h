#ifndef OMPHALOS_PCAP
#define OMPHALOS_PCAP

#ifdef __cplusplus
extern "C" {
#endif

#include <pcap.h>
#include <stddef.h>
#include <stdint.h>

struct interface;
struct pcap_pkthdr;
struct omphalos_ctx;
struct omphalos_iface;

// Input from a PCAP file
int init_pcap(const struct omphalos_ctx *);
int handle_pcap_file(const struct omphalos_ctx *);
int print_pcap_stats(FILE *fp,struct interface *);
void cleanup_pcap(const struct omphalos_ctx *);

// Output to a PCAP savefile
pcap_dumper_t *init_pcap_write(pcap_t **,const char *);

struct pcap_ll { // see pcap-datalink(7), "DLT_LINUX_SSL"
	uint16_t pkttype;		// Packet type, NBO
					//  0 for unicast to us
					//  1 for broadcast to us
					//  2 for multicast to us
					//  3 for unicast remote-to-remote
					//  4 for sent by us
	uint16_t arphrd;		// Linux ARPHRD_* value, NBO
	uint16_t llen;			// Link-layer addrlen, NBO
	uint64_t haddr;			// Up to 8 bytes of LL header
	uint16_t ethproto;		// Ethernet protocol tpye, NBO
					//  1 for Novell 802.3, 4 for 802.2 LLC
} __attribute__ ((packed));

int log_pcap_packet(struct pcap_pkthdr *,void *,size_t,const struct pcap_ll *);

#ifdef __cplusplus
}
#endif

#endif
