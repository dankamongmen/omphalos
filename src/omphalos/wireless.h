#ifndef OMPHALOS_WIRELESS
#define OMPHALOS_WIRELESS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

struct iw_event;
struct interface;
struct wless_info;
struct omphalos_iface;

int handle_wireless_event(const struct omphalos_iface *,struct interface *,
				const struct iw_event *,size_t);
int iface_wireless_info(const struct omphalos_iface *,const char *,
				struct wless_info *);

#ifdef __cplusplus
}
#endif

#endif
