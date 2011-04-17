#ifndef OMPHALOS_INTERFACE
#define OMPHALOS_INTERFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef struct interface {
	uintmax_t pkts;
	uintmax_t malformed;
	uintmax_t truncated;
	uintmax_t noprotocol;
	unsigned arptype;
	int mtu;		// to match netdevice(7)'s ifr_mtu...
	char *name;
	void *addr;		// multiple hwaddrs are multiple ifaces...
	size_t addrlen;
	int fd;			// TX packet socket
} interface;

int init_interfaces(void);
interface *iface_by_idx(int);
int print_iface_stats(FILE *,const interface *,interface *,const char *);
char *hwaddrstr(const interface *);
void free_iface(interface *);
void cleanup_interfaces(void);
int print_all_iface_stats(FILE *,interface *);

#ifdef __cplusplus
}
#endif

#endif
