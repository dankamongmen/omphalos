#ifndef OMPHALOS_NETLINK
#define OMPHALOS_NETLINK

#ifdef __cplusplus
extern "C" {
#endif

struct omphalos_iface;

int netlink_socket(void);
int discover_addrs(int);
int discover_links(int);
int discover_routes(int);
int discover_neighbors(int);
int handle_netlink_event(const struct omphalos_iface *,int);

#ifdef __cplusplus
}
#endif

#endif
