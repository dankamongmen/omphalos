#ifndef OMPHALOS_INTERFACE
#define OMPHALOS_INTERFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <netinet/in.h>
//#include <linux/in6.h>
#include <linux/ethtool.h>
#include <linux/if_packet.h>

typedef struct ip4route {
	struct in_addr dst,via;
	unsigned maskbits;		// 0..31
	int iif;			// input iface, -1 if unspecified
	struct ip4route *next;
} ip4route;

typedef struct ip6route {
	struct in6_addr dst,via;
	unsigned maskbits;		// 0..127
	int iif;			// input iface, -1 if unspecified
	struct ip6route *next;
} ip6route;

typedef struct interface {
	uintmax_t frames;	// Statistics
	uintmax_t malformed;
	uintmax_t truncated;
	uintmax_t noprotocol;

	unsigned arptype;	// from rtnetlink(7) ifi_type
	unsigned flags;		// from rtnetlink(7) ifi_flags
	int mtu;		// to match netdevice(7)'s ifr_mtu...
	char *name;
	void *addr;		// multiple hwaddrs are multiple ifaces...
	size_t addrlen;
	int fd;			// TX packet socket
	void *txm;		// TX packet ring buffer
	size_t ts;		// TX packet ring size in bytes
	struct tpacket_req ttpr;// TX packet ring descriptor
	struct ethtool_drvinfo drv;	// ethtool driver info
	// Other interfaces might also offer routes to these same
	// destinations -- they must not be considered unique!
	struct ip4route *ip4r;	// list of IPv4 routes
	struct ip6route *ip6r;	// list of IPv6 routes

	void *opaque;		// opaque callback state
} interface;

int init_interfaces(void);
interface *iface_by_idx(int);
int print_iface_stats(FILE *,const interface *,interface *,const char *);
char *hwaddrstr(const interface *);
void free_iface(interface *);
void cleanup_interfaces(void);
int print_all_iface_stats(FILE *,interface *);
int add_route4(interface *,const struct in_addr *,const struct in_addr *,
						unsigned,int);
int add_route6(interface *,const struct in6_addr *,const struct in6_addr *,
						unsigned,int);
int del_route4(interface *,const struct in_addr *,unsigned);
int del_route6(interface *,const struct in6_addr *,unsigned);

// predicates. racey against netlink messages.
int is_local4(const interface *,uint32_t);
int is_local6(const interface *,const struct in6_addr *);

// FIXME ought generate string
int print_iface(FILE *,const interface *);

#ifdef __cplusplus
}
#endif

#endif
