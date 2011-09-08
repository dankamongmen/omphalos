#ifndef OMPHALOS_PCAP
#define OMPHALOS_PCAP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct interface;
struct pcap_pkthdr;
struct omphalos_ctx;
struct omphalos_iface;

int init_pcap(const struct omphalos_ctx *);
int handle_pcap_file(const struct omphalos_ctx *);
int print_pcap_stats(FILE *fp,struct interface *);
int log_pcap_packet(struct pcap_pkthdr *,void *);
void cleanup_pcap(const struct omphalos_ctx *);

#ifdef __cplusplus
}
#endif

#endif
