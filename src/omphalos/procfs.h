#ifndef OMPHALOS_PROCFS
#define OMPHALOS_PROCFS

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct omphalos_iface;

// Load information from /proc/sys/net and /proc/net, and watch it for changes.
// Supply the root of the mounted proc filesystem as the second param.
int init_procfs(const struct omphalos_iface *,const char *);

// -1 -- unknown, 0 -- no, 1 -- yes
typedef struct procfs_state {
	int ipv4_forwarding,ipv6_forwarding;
} procfs_state;

int get_procfs_state(procfs_state *);

int cleanup_procfs(const struct omphalos_iface *);

#ifdef __cplusplus
}
#endif

#endif
