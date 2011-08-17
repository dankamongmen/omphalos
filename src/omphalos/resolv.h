#ifndef OMPHALOS_RESOLV
#define OMPHALOS_RESOLV

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

struct l2host;
struct l3host;
struct interface;
struct omphalos_iface;

int queue_for_naming(struct interface *i,struct l2host *,struct l3host *)
	__attribute__ ((nonnull (1,2,3)));

int offer_resolution(const struct omphalos_iface *,int,const void *,
		const char *) __attribute__ ((nonnull (1,3,4)));

int cleanup_naming(const struct omphalos_iface *) __attribute__ ((nonnull (1)));

#ifdef __cplusplus
}
#endif

#endif
