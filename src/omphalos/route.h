#ifndef OMPHALOS_ROUTE
#define OMPHALOS_ROUTE

#ifdef __cplusplus
extern "C" {
#endif

#include <omphalos/128.h>

struct l2host;
struct l3host;
struct nlmsghdr;
struct interface;

int handle_rtm_delroute(const struct nlmsghdr *);
int handle_rtm_newroute(const struct nlmsghdr *);

struct routepath {
	struct interface *i;
	struct l2host *l2;
	struct l3host *l3;
	uint128_t src;	// FIXME
};

// Determine how to send a packet to a layer 3 address.
int get_router(int,const void *,struct routepath *);

// Determine whether the address is known to be a route
int is_router(int,const void *);

// Call get_router() on the address, acquire a TX frame from the discovered
// interface, and fill in its layer 2 and layer 3 headers appropriately,
int get_routed_frame(int,const void *,struct routepath *,void **,size_t *,size_t *);

void free_routes(void);

#ifdef __cplusplus
}
#endif

#endif
