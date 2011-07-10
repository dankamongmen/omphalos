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
					const void *,size_t);

void cleanup_l2hosts(struct l2host **);
int print_l2hosts(FILE *,const struct l2host *);
char *l2addrstr(const struct l2host *,size_t);

void *l2host_get_opaque(struct l2host *);

// Predicates and comparators
int l2hostcmp(const struct l2host *,const struct l2host *);
int l2categorize(const struct interface *,const struct l2host *);

// Naming
void name_l2host(const struct interface *,struct l2host *,int,const void *);
const char *get_name(const struct l2host *);

#ifdef __cplusplus
}
#endif

#endif
