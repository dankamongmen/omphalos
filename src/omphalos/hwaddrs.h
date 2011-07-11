#ifndef OMPHALOS_HWADDR
#define OMPHALOS_HWADDR

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>

struct l2host;
struct interface;
struct omphalos_iface;

struct l2host *lookup_l2host(const struct omphalos_iface *,struct interface *,
				const void *,size_t,int,const void *)
				__attribute__ ((nonnull (1,2,3)));

void cleanup_l2hosts(struct l2host **);
int print_l2hosts(FILE *,const struct l2host *);
char *l2addrstr(const struct l2host *,size_t);

void *l2host_get_opaque(struct l2host *);
void *l2host_set_opaque(struct l2host *l2,void *);

// Predicates and comparators
int l2hostcmp(const struct l2host *,const struct l2host *);
int l2categorize(const struct interface *,const struct l2host *);

// Naming
void name_l2host(const struct omphalos_iface *,const struct interface *,
				struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,5)));
void name_l2host_local(const struct omphalos_iface *,const struct interface *,
				struct l2host *,int,const void *)
				__attribute__ ((nonnull (1,2,3,5)));

const char *get_name(const struct l2host *);

#ifdef __cplusplus
}
#endif

#endif
