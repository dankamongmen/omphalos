#ifndef OMPHALOS_PROCFS
#define OMPHALOS_PROCFS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Load information from /proc/sys/net and /proc/net, and watch it for changes.
// Supply the root of the mounted proc filesystem as the second param.
int init_procfs(const char *);

// -1 -- unknown, 0 -- no, 1 -- yes
typedef struct procfs_state {
	int ipv4_forwarding,ipv6_forwarding;	// global forwarding
	int proxyarp;				// global proxy arp
	int rp_filter;				// global reverse path filter
	char *tcp_ccalg;			// tcp congestion control
	int tcp_sack,tcp_fack;			// selective ACKs, forward ACK
	int tcp_dsack,tcp_frto;			// dup SACK, forward RTO recov
} procfs_state;

int get_procfs_state(procfs_state *);
void clean_procfs_state(procfs_state *);

int cleanup_procfs(void);

#ifdef __cplusplus
}
#endif

#endif
