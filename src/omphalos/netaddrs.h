#ifndef OMPHALOS_NETADDRS
#define OMPHALOS_NETADDRS

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdint.h>
#include <omphalos/128.h>

struct l2host;
struct l3host;
struct interface;

#define AF_BSSID (AF_MAX + 1)

typedef enum {
	NAMING_LEVEL_NONE,	// No name
	NAMING_LEVEL_RESOLVING,	// Currently being looked up
	NAMING_LEVEL_FAIL,	// Failure sending query, might retransmit
	NAMING_LEVEL_NXDOMAIN,	// Server returned Name Error
	NAMING_LEVEL_DNS,	// Direct lookup maps multiple names to an IP
	NAMING_LEVEL_REVDNS,	// Reverse lookup is unique for each IP
	NAMING_LEVEL_GLOBAL,	// Globally-assigned address
	NAMING_LEVEL_MAX
} namelevel;

// Look up an l3 address, creating an l3host if the address isn't known on
// this l2host. A route check will be performed; if no local route to this host
// exists, an ARP request will be issued rather than adding the host.
struct l3host *lookup_l3host(struct interface *,struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,4)));

// Look up an l3 address known to be local (perhaps we got it from the host's
// ARP cache, or it's our own address). No ARP/route lookup will be performed.
struct l3host *lookup_local_l3host(struct interface *,struct l2host *,int,const void *)
		__attribute__ ((nonnull (1,2,4)));

// Look up an l3 address on this interface, ignoring l2host information. Does
// not create an l3host on lookup failure.
struct l3host *find_l3host(struct interface *,int,const void *)
	__attribute__ ((nonnull (1,3)));

// Doesn't create the l3host if it isn't found (what interface would it bind it
// to?), but scans all interfaces' nodes for such a host.
struct l3host *lookup_global_l3host(int,const void *) __attribute__ ((nonnull (2)));

void name_l3host_local(const struct interface *,struct l2host *,struct l3host *,int,const void *,namelevel)
				__attribute__ ((nonnull (1,2,3,5)));

void name_l3host_absolute(const struct interface *,struct l2host *,struct l3host *,const char *,namelevel)
				__attribute__ ((nonnull (1,2,3,4)));

void wname_l3host_absolute(const struct interface *,struct l2host *,struct l3host *,const wchar_t *,namelevel)
				__attribute__ ((nonnull (1,2,3,4)));

char *l3addrstr(const struct l3host *) __attribute__ ((nonnull (1)));
char *netaddrstr(int,const void *) __attribute__ ((nonnull (2)));

// Get a string representation of the l3host's network address
int l3ntop(const struct l3host *,char *,size_t) __attribute__ ((nonnull (1,2)));

void cleanup_l3hosts(struct l3host **list) __attribute__ ((nonnull (1)));

// Accessors
const wchar_t *get_l3name(const struct l3host *) __attribute__ ((nonnull (1)));
namelevel get_l3nlevel(const struct l3host *) __attribute__ ((nonnull (1)));
void *l3host_get_opaque(struct l3host *) __attribute__ ((nonnull (1)));
uintmax_t l3_get_srcpkt(const struct l3host *) __attribute__ ((nonnull (1)));
uintmax_t l3_get_dstpkt(const struct l3host *) __attribute__ ((nonnull (1)));
uint32_t get_l3addr_in(const struct l3host *) __attribute__ ((nonnull (1)));
const uint128_t *get_l3addr_in6(const struct l3host *) __attribute__ ((nonnull (1)));
struct l2host *l3_getlastl2(struct l3host *) __attribute__ ((nonnull (1)));

// Services (UDP/TCP, generally). l3_getservices() returns the head of the
// structure. l3_setservices() sets the head of the structure. The structure
// itself is managed by services.c. Obviously, some locking must be active
// across the calls to l3_getservices() and a subsequenct l3_setservices() or
// any other use of the service structure. If you'll only be walking the
// structure, use l3_getconstservices() for const enforcement.
struct l4srv *l3_getservices(struct l3host *);
const struct l4srv *l3_getconstservices(const struct l3host *);
void l3_setservices(struct l3host *,struct l4srv *);

// Predicates
int l3addr_eq_p(const struct l3host *,int,const void *) __attribute__ ((nonnull (1,3)));

// Statistics
void l3_srcpkt(struct l3host *) __attribute__ ((nonnull (1)));
void l3_dstpkt(struct l3host *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
