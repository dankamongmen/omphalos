#ifndef OMPHALOS_NETADDRS
#define OMPHALOS_NETADDRS

#ifdef __cplusplus
extern "C" {
#endif

struct l2host;
struct l3host;
struct interface;
struct omphalos_iface;

#define AF_BSSID (AF_MAX + 1)

struct l3host *lookup_l3host(const struct omphalos_iface *,struct interface *,
				struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,5)));

void name_l3host_local(const struct omphalos_iface *,const struct interface *,
			struct l2host *,struct l3host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,4,6)));

void name_l3host_absolute(const struct omphalos_iface *,const struct interface *,
			struct l2host *,struct l3host *,const char *)
				__attribute__ ((nonnull (1,2,3,4,5)));

char *l3addrstr(const struct l3host *) __attribute__ ((nonnull (1)));

int l3ntop(const struct l3host *,char *,size_t) __attribute__ ((nonnull (1,2)));

void cleanup_l3hosts(struct l3host **list) __attribute__ ((nonnull (1)));

// Accessors
const char *get_l3name(const struct l3host *) __attribute__ ((nonnull (1)));
void *l3host_get_opaque(struct l3host *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
