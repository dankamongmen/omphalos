#ifndef OMPHALOS_HWADDR
#define OMPHALOS_HWADDR

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>

struct l2host;
struct interface;

struct l2host *lookup_l2host(struct l2host **,const void *,size_t);
void cleanup_l2hosts(struct l2host **);
int print_l2hosts(FILE *,const struct l2host *);
char *l2addrstr(const struct l2host *,size_t);
int print_neigh(const struct interface *,const struct l2host *);

void *l2host_get_opaque(struct l2host *);
void l2host_set_opaque(struct l2host *,void *);

// Predicates and comparators
int l2hostcmp(const struct l2host *,const struct l2host *);
int l2categorize(const struct interface *,const struct l2host *);
void name_l2host(struct l2host *,int,const void *);

#ifdef __cplusplus
}
#endif

#endif
