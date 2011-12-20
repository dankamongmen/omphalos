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
	uint16_t pkttype;
	uint16_t arphrd;
	uint16_t llen;
	uint64_t haddr;
	uint16_t ethproto;
} __attribute__ ((packed));

int log_pcap_packet(struct pcap_pkthdr *,void *,size_t,const struct pcap_ll *);

#ifdef __cplusplus
}
#endif

#endif
