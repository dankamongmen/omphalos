#ifndef OMPHALOS_ROUTE
#define OMPHALOS_ROUTE

#ifdef __cplusplus
extern "C" {
#endif

struct nlmsghdr;
struct omphalos_iface;

int handle_rtm_delroute(const struct omphalos_iface *,const struct nlmsghdr *);
int handle_rtm_newroute(const struct omphalos_iface *,const struct nlmsghdr *);

#ifdef __cplusplus
}
#endif

#endif
