#ifndef OMPHALOS_WIRELESS
#define OMPHALOS_WIRELESS

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <linux/version.h>
#include <linux/nl80211.h>

struct iw_event;
struct interface;
struct wless_info;
struct omphalos_iface;

int handle_wireless_event(const struct omphalos_iface *,struct interface *,
				const struct iw_event *,size_t);
int iface_wireless_info(const char *,struct wless_info *)
				__attribute__ ((nonnull (1,2)));

#define MAX_WIRELESS_CHANNEL		999

static inline const char *
modestr(unsigned dplx){
	switch(dplx){
		case NL80211_IFTYPE_UNSPECIFIED: return "auto"; break;
		case NL80211_IFTYPE_ADHOC: return "adhoc"; break;
		case NL80211_IFTYPE_STATION: return "managed"; break;
		case NL80211_IFTYPE_AP: return "ap"; break;
		case NL80211_IFTYPE_AP_VLAN: return "apvlan"; break;
		case NL80211_IFTYPE_WDS: return "wds"; break;
		case NL80211_IFTYPE_MONITOR: return "monitor"; break;
		case NL80211_IFTYPE_MESH_POINT: return "mesh"; break;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)
		case NL80211_IFTYPE_P2P_CLIENT: return "p2pclient"; break;
		case NL80211_IFTYPE_P2P_GO: return "p2pgo"; break;
#endif
		default: break;
	}
	return "";
}

int wireless_idx_byfreq(unsigned);
unsigned wireless_freq_count(void);
unsigned wireless_freq_byidx(unsigned);
unsigned wireless_chan_byidx(unsigned);

// Returns dBm supported on frequency, if any
float wireless_freq_supported_byidx(const struct interface *,unsigned);

#ifdef __cplusplus
}
#endif

#endif
