#ifndef OMPHALOS_NL80211
#define OMPHALOS_NL80211

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct interface;

#define MAX_WIRELESS_CHANNEL		999

typedef struct nl80211_info {
	uintmax_t freqs[MAX_WIRELESS_CHANNEL];
} nl80211_info;

int open_nl80211(void);
int close_nl80211(void);

// Get nl80211 settings, if available, for the specified interface
int iface_nl80211_info(const struct interface *,nl80211_info *);

#ifdef __cplusplus
}
#endif

#endif
