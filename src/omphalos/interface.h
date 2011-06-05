#ifndef OMPHALOS_INTERFACE
#define OMPHALOS_INTERFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <netinet/in.h>
#include <linux/ethtool.h>
#include <linux/if_packet.h>

struct omphalos_iface;

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

typedef struct wireless_info {
	unsigned bitrate;
	unsigned mode;
} wireless_info;

typedef struct interface {
	uintmax_t frames;		// Frames received on the interface
	uintmax_t malformed;		// Packet had malformed L2 -- L4 headers
	uintmax_t truncated;		// Packet didn't fit in ringbuffer frame
	uintmax_t truncated_recovered;	// We were able to recvfrom() the packet
	uintmax_t noprotocol;		// Packets without protocol handler
	uintmax_t bytes;		// Total bytes sniffed

	// For recvfrom()ing truncated packets (see PACKET_COPY_THRESH sockopt)
	void *truncbuf;
	size_t truncbuflen;

	unsigned arptype;	// from rtnetlink(7) ifi_type
	unsigned flags;		// from rtnetlink(7) ifi_flags
	int mtu;		// to match netdevice(7)'s ifr_mtu...
	char *name;
	void *addr;		// multiple hwaddrs are multiple ifaces...
	size_t addrlen;
	pthread_t tid;		// packet socket thread
	int rfd;		// RX packet socket
	void *rxm;		// RX packet ring buffer
	size_t rs;		// RX packet ring size in bytes
	struct tpacket_req rtpr;// RX packet ring descriptor
	int fd;			// TX packet socket
	void *txm;		// TX packet ring buffer
	size_t ts;		// TX packet ring size in bytes
	struct tpacket_req ttpr;// TX packet ring descriptor
	struct ethtool_drvinfo drv;	// ethtool driver info
	const char *busname;	// "pci", "usb" etc (from sysfs/bus/)
	enum {
		SETTINGS_INVALID,
		SETTINGS_VALID_ETHTOOL,
		SETTINGS_VALID_WEXT,
	} settings_valid;	// set if the settings field can be trusted
	union {
		struct ethtool_cmd ethtool;	// ethtool settings info
		struct wireless_info wext;	// wireless extensions info
	} settings;
	// Other interfaces might also offer routes to these same
	// destinations -- they must not be considered unique!
	struct ip4route *ip4r;	// list of IPv4 routes
	struct ip6route *ip6r;	// list of IPv6 routes

	struct l2host *l2hosts;

	void *opaque;		// opaque callback state
} interface;

int init_interfaces(void);
interface *iface_by_idx(int);
int print_iface_stats(FILE *,const interface *,interface *,const char *);
char *hwaddrstr(const interface *);
void free_iface(const struct omphalos_iface *,interface *);
void cleanup_interfaces(const struct omphalos_iface *);
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

const char *lookup_arptype(unsigned);

int enable_promiscuity(const struct omphalos_iface *,const interface *);
int disable_promiscuity(const struct omphalos_iface *,const interface *);
int up_interface(const struct omphalos_iface *,const interface *);
int down_interface(const struct omphalos_iface *,const interface *);

#ifdef __cplusplus
}
#endif

#endif
