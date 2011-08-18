#ifndef OMPHALOS_NETADDRS
#define OMPHALOS_NETADDRS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <omphalos/128.h>

struct l2host;
struct l3host;
struct interface;
struct omphalos_iface;

#define AF_BSSID (AF_MAX + 1)

typedef enum {
	NAMING_LEVEL_NONE,	// No name
	NAMING_LEVEL_DNS,	// Direct lookup maps multiple names to an IP
	NAMING_LEVEL_REVDNS,	// Reverse lookup is unique for each IP
	NAMING_LEVEL_MAX
} namelevel;

struct l3host *lookup_l3host(const struct omphalos_iface *,struct interface *,
				struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,5)));

void name_l3host_local(const struct omphalos_iface *,const struct interface *,
			struct l2host *,struct l3host *,int,const void *,namelevel)
				__attribute__ ((nonnull (1,2,3,4,6)));

void name_l3host_absolute(const struct omphalos_iface *,const struct interface *,
			struct l2host *,struct l3host *,const char *,namelevel)
				__attribute__ ((nonnull (1,2,3,4,5)));

char *l3addrstr(const struct l3host *) __attribute__ ((nonnull (1)));
char *netaddrstr(int,const void *) __attribute__ ((nonnull (2)));

int l3ntop(const struct l3host *,char *,size_t) __attribute__ ((nonnull (1,2)));

void cleanup_l3hosts(struct l3host **list) __attribute__ ((nonnull (1)));

// Accessors
const char *get_l3name(const struct l3host *) __attribute__ ((nonnull (1)));
void *l3host_get_opaque(struct l3host *) __attribute__ ((nonnull (1)));
uintmax_t l3_get_srcpkt(const struct l3host *) __attribute__ ((nonnull (1)));
uintmax_t l3_get_dstpkt(const struct l3host *) __attribute__ ((nonnull (1)));
uint32_t get_l3addr_in(const struct l3host *) __attribute__ ((nonnull (1)));
uint128_t get_l3addr_in6(const struct l3host *) __attribute__ ((nonnull (1)));

// Predicates
int router_p(const struct l3host *) __attribute__ ((nonnull (1)));
int l3addr_eq_p(const struct l3host *,int,const void *) __attribute__ ((nonnull (1,3)));

// Statistics
void l3_srcpkt(struct l3host *) __attribute__ ((nonnull (1)));
void l3_dstpkt(struct l3host *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
