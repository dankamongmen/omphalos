#ifndef OMPHALOS_OMPHALOS
#define OMPHALOS_OMPHALOS

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <pcap/pcap.h>
#include <omphalos/128.h>

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.99.1-pre"	// FIXME

struct l4srv;
struct l2host;
struct l3host;
struct iphost;
struct ipv6host;
struct interface;

// State for each packet
typedef struct omphalos_packet {
	struct timeval tv;
	struct interface *i;
	struct l2host *l2s,*l2d;
	uint16_t pcap_ethproto;		// See pcap-linktype's DLT_LINUX_SLL.
					//  This value is suitable to fill the
					//  ethproto field in a struct pcap_ll.
	uint16_t l3proto;		// Actual L3 protocol number
	struct l3host *l3s,*l3d;
	uint128_t l3saddr,l3daddr;
	uint16_t l4src,l4dst;
	unsigned malformed;
	unsigned noproto;
} omphalos_packet;

// UI callback interface. Any number may be NULL, save diagnostic.
typedef struct omphalos_iface {
	// Free-form diagnostics using standard print(3)-style format strings.
	//void (*diagnostic)(const char *,va_list);
	void (*vdiagnostic)(const char *,va_list);

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

	// L4 service event callback, fed by packet analysis. The return value
	// is treated similarly to that of the device event callback.
	void *(*srv_event)(const struct interface *,struct l2host *,
				struct l3host *,struct l4srv *);

	// Network metastatus change callback, fed by network analysis. Covers
	// everything from /proc to DNS to routing.
	void (*network_event)(void);
} omphalos_iface;

typedef enum {
	OMPHALOS_MODE_SILENT,
	//OMPHALOS_MODE_STEALTHY,
	OMPHALOS_MODE_ACTIVE,
	/*OMPHALOS_MODE_AGGRESSIVE,
	OMPHALOS_MODE_FORCEFUL,
	OMPHALOS_MODE_HOSTILE*/
	OMPHALOS_MODE_MAX
} omphalos_mode_enum;

// Process-scope settings, generally configured on startup based off
// command-line options.
typedef struct omphalos_ctx {
	const char *pcapfn;	 // PCAP-format filename FIXME support multiple?
	const char *ianafn;	 // IANA's OUI mappings in get-oui(1) format
	const char *resolvconf;	 // resolver configuration file
	const char *usbidsfn;	 // USB ID database in update-usbids(8) format
	omphalos_mode_enum mode; // operating mode
	int nopromiscuous;	 // do not make newly-discovered devices promiscous
	omphalos_iface iface;
	pcap_t *plogp;
	pcap_dumper_t *plog;
} omphalos_ctx;

// The omphalos_ctx for a given thread can be accessed via this TSD.
extern pthread_key_t omphalos_ctx_key;

// Retrieve this thread's omphalos_ctx
const omphalos_ctx *get_octx(void);

// Parse the command line for common arguments (a UI introducing its own
// CLI arguments would need to extract them before calling, as stands
// FIXME). Initializes and prepares an omphalos_ctx.
int omphalos_setup(int,char * const *,omphalos_ctx *);

// Run the provided context
int omphalos_init(const omphalos_ctx *);

void omphalos_cleanup(const omphalos_ctx *);

// Classify the packet as a sockaddr_ll-style pkttype (PACKET_HOST etc)
int packet_sll_type(const omphalos_packet *);

#ifdef __cplusplus
}
#endif

#endif
