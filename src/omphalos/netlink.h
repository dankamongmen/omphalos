#ifndef OMPHALOS_NETLINK
#define OMPHALOS_NETLINK

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

struct omphalos_iface;

int netlink_socket(void);
int discover_addrs(const struct omphalos_iface *,int);
int discover_links(const struct omphalos_iface *,int);
int discover_routes(const struct omphalos_iface *,int);
int discover_neighbors(const struct omphalos_iface *,int);
int handle_netlink_event(const struct omphalos_iface *,int);
int reap_thread(const struct omphalos_iface *,pthread_t);

#ifdef __cplusplus
}
#endif

#endif
