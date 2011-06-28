#ifndef OMPHALOS_OMPHALOS
#define OMPHALOS_OMPHALOS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct l2host;
struct iphost;
struct ipv6host;
struct interface;

typedef struct omphalos_packet {
	struct interface *i;
	struct l2host *l2s,*l2d;
	uint16_t l3proto;
	union {
		struct iphost *ip4;
		struct ipv6host *ip6;
	} l3s,l3d;
} omphalos_packet;

// UI callback interface. Any number may be NULL.
typedef struct omphalos_iface {
	// FIXME get rid of first argument, passing only omphalos_packet
	void (*packet_read)(void *,struct omphalos_packet *);
	void *(*iface_event)(struct interface *,void *);
	void (*iface_removed)(const struct interface *,void *);
	void *(*neigh_event)(const struct interface *,const struct l2host *,void *);
	void (*neigh_removed)(const struct interface *,const struct l2host *,void *);
	void *(*wireless_event)(struct interface *,unsigned,void *);
	void (*diagnostic)(const char *,...);
} omphalos_iface;

// Process-scope settings, generally configured on startup based off
// command-line options.
typedef struct omphalos_ctx {
	// PCAP-format filename FIXME support multiple?
	const char *pcapfn;

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
