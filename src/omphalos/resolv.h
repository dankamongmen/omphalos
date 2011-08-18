#ifndef OMPHALOS_RESOLV
#define OMPHALOS_RESOLV

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <omphalos/netaddrs.h>

struct l2host;
struct interface;
struct omphalos_iface;

int queue_for_naming(struct interface *i,struct l2host *,struct l3host *)
	__attribute__ ((nonnull (1,2,3)));

int offer_resolution(const struct omphalos_iface *,int,const void *,
		const char *,namelevel) __attribute__ ((nonnull (1,3,4)));

int init_naming(const struct omphalos_iface *,const char *) __attribute__ ((nonnull (1,2)));
int cleanup_naming(const struct omphalos_iface *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif