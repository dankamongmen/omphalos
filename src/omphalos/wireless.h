#ifndef OMPHALOS_WIRELESS
#define OMPHALOS_WIRELESS

#ifdef __cplusplus
extern "C" {
#endif

struct iw_event;
struct interface;

int handle_wireless_event(struct interface *,const struct iw_event *);

#ifdef __cplusplus
}
#endif

#endif
