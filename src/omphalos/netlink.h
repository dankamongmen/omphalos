#ifndef OMPHALOS_NETLINK
#define OMPHALOS_NETLINK

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

struct interface;
struct omphalos_ctx;

int netlink_socket(void);

int iplink_modify(int,int,unsigned,unsigned);

void reap_thread(struct interface *);

int netlink_thread(void);

void cancellation_signal_handler(int);

// Loop on a netlink socket according to provided program parameters
int handle_netlink_socket(void);

#ifdef __cplusplus
}
#endif

#endif
