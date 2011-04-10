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
} interface;

int print_iface_stats(FILE *,const interface *,interface *,const char *);

#ifdef __cplusplus
}
#endif

#endif
