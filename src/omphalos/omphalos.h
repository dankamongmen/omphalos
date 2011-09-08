#ifndef OMPHALOS_OMPHALOS
#define OMPHALOS_OMPHALOS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/time.h>

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98-pre"	// FIXME

struct l2host;
struct l3host;
struct iphost;
struct ipv6host;
struct interface;

typedef struct omphalos_packet {
	struct timeval tv;
	struct interface *i;
	struct l2host *l2s,*l2d;
	uint16_t l3proto;
	struct l3host *l3s,*l3d;
	/*union {
		struct iphost *ip4;
		struct ipv6host *ip6;
	} l3s,l3d;*/
} omphalos_packet;

// UI callback interface. Any number may be NULL, save diagnostic.
typedef struct omphalos_iface {
	// Free-form diagnostics using standard print(3)-style format strings.
	void (*diagnostic)(const char *,...);

	// Device event callback. Called upon device detection or change. A
	// value returned will be associated with the interface's "opaque"
	// member. Until this has happened, the interface's "opaque" member
	// will be NULL. Only upon a non-NULL return will packet callbacks be
	// invoked for this device.
	void *(*iface_event)(struct interface *,void *);

	// A device event augmented with wireless datails.
	void *(*wireless_event)(struct interface *,unsigned,void *);

	// Called for each packet read. Will not be called prior to a successful
	// invocation of the device event callback, without an intervening 
	// device removal callback.
	void (*packet_read)(struct omphalos_packet *);

	// Device removal callback. Following this call, no packet callbacks
	// will be invoked for this interface until a succesful device event
	// callback is performed. This callback will not be invoked while a
	// packet or device event callback is being invoked for the interface.
	// It might be invoked without a corresponding device event callback.
	void (*iface_removed)(const struct interface *,void *);

	// L2 neighbor event callback, fed by packet analysis and netlink
	// neighbor cache events. The return value is treated similarly to that
	// of the device event callback.
	void *(*neigh_event)(const struct interface *,struct l2host *);
	
	// L3 network event callback, fed by packet analysis and netlink
	// neighbor cache events. The return value is treated similarly to that
	// of the device event callback.
	void *(*host_event)(const struct interface *,struct l2host *,struct l3host *);
} omphalos_iface;

// Process-scope settings, generally configured on startup based off
// command-line options.
typedef struct omphalos_ctx {
	// PCAP-format filename FIXME support multiple?
	const char *pcapfn;
	const char *ianafn;	// IANA's OUI mappings in get-oui(1) format
	const char *resolvconf;	// resolver configuration file
	omphalos_iface iface;
} omphalos_ctx;

// Parse the command line for common arguments (a UI introducing its own
// CLI arguments would need to extract them before calling, as stands
// FIXME). Initializes and prepares an omphalos_ctx.
int omphalos_setup(int,char * const *,omphalos_ctx *);

// Run the provided context
int omphalos_init(const omphalos_ctx *);

void omphalos_cleanup(const omphalos_ctx *);

#ifdef __cplusplus
}
#endif

#endif
