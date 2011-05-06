#ifndef OMPHALOS_WIRELESS
#define OMPHALOS_WIRELESS

#ifdef __cplusplus
extern "C" {
#endif

struct iw_event;
struct interface;
struct omphalos_iface;

int handle_wireless_event(const struct omphalos_iface *,struct interface *,
				const struct iw_event *,size_t);

#ifdef __cplusplus
}
#endif

#endif
