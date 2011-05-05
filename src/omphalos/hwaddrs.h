#ifndef OMPHALOS_HWADDR
#define OMPHALOS_HWADDR

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>

struct l2host;
struct interface;

struct l2host *lookup_l2host(const void *,size_t);
void cleanup_l2hosts(void);
int print_l2hosts(FILE *);
char *l2addrstr(const void *,size_t);
int print_neigh(const struct interface *,const struct l2host *);

#ifdef __cplusplus
}
#endif

#endif
