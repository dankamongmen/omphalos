#ifndef OMPHALOS_PCAP
#define OMPHALOS_PCAP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct interface;
struct omphalos_ctx;
struct omphalos_iface;

int handle_pcap_file(const struct omphalos_ctx *);
int print_pcap_stats(FILE *fp,struct interface *);
void cleanup_pcap(const struct omphalos_ctx *);

#ifdef __cplusplus
}
#endif

#endif
