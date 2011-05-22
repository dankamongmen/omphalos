#ifndef OMPHALOS_WIRELESS
#define OMPHALOS_WIRELESS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct iw_event;
struct interface;
struct wireless_info;
struct omphalos_iface;

int handle_wireless_event(const struct omphalos_iface *,struct interface *,
				const struct iw_event *,size_t);
int print_wireless_event(FILE *,const struct interface *,unsigned);
int iface_wireless_info(const struct omphalos_iface *,const char *,
				struct wireless_info *);

#ifdef __cplusplus
}
#endif

#endif
