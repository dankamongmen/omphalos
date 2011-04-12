#ifndef OMPHALOS_NETLINK
#define OMPHALOS_NETLINK

#ifdef __cplusplus
extern "C" {
#endif

int netlink_socket(void);
int discover_links(int);
int handle_netlink_event(int);

#ifdef __cplusplus
}
#endif

#endif
