#ifndef OMPHALOS_NL80211
#define OMPHALOS_NL80211

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <omphalos/wireless.h>

struct interface;

typedef struct nl80211_info {
	float dBm[MAX_WIRELESS_CHANNEL];	// Set if enabled
	int chan;				// Current channel or -1
	uintmax_t freq;				// Current freq or 0
	unsigned mode;				// Current mode
	unsigned bitrate;			// Current bitrate or 0
} nl80211_info;

int open_nl80211(void);
int close_nl80211(void);

// Get nl80211 settings, if available, for the specified interface
int iface_nl80211_info(const struct interface *,nl80211_info *);

#ifdef __cplusplus
}
#endif

#endif
