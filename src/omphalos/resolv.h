#ifndef OMPHALOS_RESOLV
#define OMPHALOS_RESOLV

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <omphalos/netaddrs.h>

struct l2host;
struct interface;

typedef int
(*dnstxfxn)(int,const void *,const char *);

int queue_for_naming(struct interface *i,struct l3host *,dnstxfxn,
				const char *,int,const void *)
			__attribute__ ((nonnull (1,2,3,4,6)));

int offer_wresolution(int,const void *,const wchar_t *,namelevel,
		int,const void *) __attribute__ ((nonnull (2,3,6)));

int offer_resolution(int,const void *,const char *,namelevel,int,const void *)
			__attribute__ ((nonnull (2,3,6)));

void offer_nameserver(int,const void *);

int init_naming(const char *) __attribute__ ((nonnull (1)));
int cleanup_naming(void);

char *stringize_resolvers(void);

#ifdef __cplusplus
}
#endif

#endif
