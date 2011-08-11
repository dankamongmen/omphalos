#ifndef OMPHALOS_NETADDRS
#define OMPHALOS_NETADDRS

#ifdef __cplusplus
extern "C" {
#endif

struct l2host;
struct l3host;
struct interface;
struct omphalos_iface;

struct l3host *lookup_l3host(const struct omphalos_iface *,struct interface *,
				struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,5)));

void name_l3host(const struct omphalos_iface *,struct interface *,
			struct l2host *,struct l3host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,4,6)));

void name_l3host_local(const struct omphalos_iface *,const struct interface *,
			struct l2host *,struct l3host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,4,6)));

char *l3addrstr(const struct l3host *) __attribute__ ((nonnull (1)));

int l3ntop(const struct l3host *,char *,size_t) __attribute__ ((nonnull (1,2)));

// Accessors
const char *get_l3name(const struct l3host *) __attribute__ ((nonnull (1)));
void *l3host_get_opaque(struct l3host *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
