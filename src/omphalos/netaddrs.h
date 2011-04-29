#ifndef OMPHALOS_NETADDRS
#define OMPHALOS_NETADDRS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>

struct iphost;
struct ipv6host;
struct interface;

struct iphost *lookup_iphost(const struct interface *,const void *);
struct ipv6host *lookup_ipv6host(const struct interface *,const void *);
char *ipaddrstr(const void *);
char *ipv6addrstr(const void *);

void cleanup_l3hosts(void);
int print_l3hosts(FILE *);

#ifdef __cplusplus
}
#endif

#endif
