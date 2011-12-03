#ifndef OMPHALOS_QUERIES
#define OMPHALOS_QUERIES

#ifdef __cplusplus
extern "C" {
#endif

struct interface;

int query_network(int,struct interface *,const void *);

#ifdef __cplusplus
}
#endif

#endif
