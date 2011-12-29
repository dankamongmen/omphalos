#include <errno.h>
#include <iwlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <omphalos/diag.h>
#include <linux/wireless.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static inline int
get_wireless_extension(const char *name,int cmd,struct iwreq *req){
	int fd;

	if(strlen(name) >= sizeof(req->ifr_name)){
		diagnostic("Name too long: %s",name);
		return -1;
	}
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		diagnostic("Couldn't get a socket (%s?)",strerror(errno));
		return -1;
	}
	strcpy(req->ifr_name,name);
	if(ioctl(fd,cmd,req)){
		//diagnostic("ioctl() failed (%s?)",strerror(errno));
		close(fd);
		return -1;
	}
	if(close(fd)){
		diagnostic("Couldn't close socket (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

static int
wireless_rate_info(const char *name,wless_info *wi){
	const struct iw_param *ip;
	struct iwreq req;

	if(get_wireless_extension(name,SIOCGIWRATE,&req)){
		return -1;
	}
	ip = &req.u.bitrate;
	wi->bitrate = ip->value;
	return 0;
}

static inline uintmax_t
iwfreq_defreak(const struct iw_freq *iwf){
	uintmax_t ret = iwf->m;
	unsigned e = iwf->e;

	while(e--){
		ret *= 10;
	}
	return ret;
}

static int
wireless_freq_info(const char *name,wless_info *wi){
	struct iw_range range;
	unsigned f;
	int fd;

	assert(wi);
	if((fd = socket(AF_INET,SOCK_DGRAM,0)) < 0){
		diagnostic("Couldn't get a socket (%s?)",strerror(errno));
		return -1;
	}
	if(iw_get_range_info(fd,name,&range)){
		diagnostic("Couldn't get range info on %s (%s)",name,strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	for(f = 0 ; f < range.num_frequency ; ++f){
		uintmax_t freq = iwfreq_defreak(&range.freq[f]);
		int idx = wireless_idx_byfreq(freq);

		if(idx < 0){
			diagnostic("Unknown frequency: %ju",freq);
			return -1;
		}
		wi->dBm[idx] = 1.0; // FIXME get real maxstrength
	}
	return 0;
}

int handle_wireless_event(const omphalos_iface *octx,interface *i,
				const struct iw_event *iw,size_t len){
	if(len < IW_EV_LCP_LEN){
		diagnostic("Wireless msg too short on %s (%zu)",i->name,len);
		return -1;
	}
	switch(iw->cmd){
	case SIOCGIWSCAN:{
		// FIXME handle scan results
	break;}case SIOCGIWAP:{
		// FIXME handle AP results
	break;}case SIOCGIWSPY:{
		// FIXME handle AP results
	break;}case SIOCSIWMODE:{
		// FIXME handle wireless mode change
	break;}case SIOCSIWFREQ:{
		// FIXME handle frequency/channel change
	break;}case IWEVASSOCRESPIE:{
		// FIXME handle IE reassociation results
	break;}case SIOCSIWESSID:{
		// FIXME handle ESSID change
	break;}case SIOCSIWRATE:{
		// FIXME doesn't this come as part of the netlink message? this
		// is an extra 3 system calls...
		wireless_rate_info(i->name,&i->settings.wext);
	break;}case SIOCSIWTXPOW:{
		// FIXME handle TX power change
	break;}default:{
		diagnostic("Unknown wireless event on %s: 0x%x",i->name,iw->cmd);
		return -1;
	} }
	if(octx->wireless_event){
		i->opaque = octx->wireless_event(i,iw->cmd,i->opaque);
	}
	return 0;
}

int iface_wireless_info(const char *name,wless_info *wi){
	struct iwreq req;

	memset(wi,0,sizeof(*wi));
	memset(&req,0,sizeof(req));
	if(get_wireless_extension(name,SIOCGIWNAME,&req)){
		return -1;
	}
	if(wireless_rate_info(name,wi)){
		wi->bitrate = 0; // no bitrate for eg monitor mode
	}
	if(wireless_freq_info(name,wi)){
		return -1;
	}
	if(get_wireless_extension(name,SIOCGIWMODE,&req)){
		return -1;
	}
	wi->mode = req.u.mode;
	if(get_wireless_extension(name,SIOCGIWFREQ,&req)){
		wi->freq = 0; // no frequency for eg unassociated managed mode
	}else{
		wi->freq = iwfreq_defreak(&req.u.freq);
	}
	return 0;
}

#define FREQ_80211A	0x01
#define FREQ_80211B	0x02
#define FREQ_80211G	0x04
#define FREQ_80211N	0x08
#define FREQ_80211Y	0x10
#define FREQ_24		(FREQ_80211B|FREQ_80211G|FREQ_80211N)
#define FREQ_36		(FREQ_80211Y)
#define FREQ_5		(FREQ_80211A|FREQ_80211N)
#define MHZ(hz)		(hz * 1000000ull)
#define HMHZ(hz)	(hz * 100000ull)
static const struct freq {
	uint64_t hz;		// unique Hz
	unsigned channel;	// channel #. multiple freqs per channel!
	unsigned modes;		// bitmask of FREQ_* values
} freqtable[] = {
	{ MHZ(2412),		1,	FREQ_24,	},
	{ MHZ(2417),		2,	FREQ_24,	},
	{ MHZ(2422),		3,	FREQ_24,	},
	{ MHZ(2427),		4,	FREQ_24,	},
	{ MHZ(2432),		5,	FREQ_24,	},
	{ MHZ(2437),		6,	FREQ_24,	},
	{ MHZ(2442),		7,	FREQ_24,	},
	{ MHZ(2447),		8,	FREQ_24,	},
	{ MHZ(2452),		9,	FREQ_24,	},
	{ MHZ(2457),		10,	FREQ_24,	},
	{ MHZ(2462),		11,	FREQ_24,	},
	{ MHZ(2467),		12,	FREQ_24,	},
	{ MHZ(2472),		13,	FREQ_24,	},
	{ MHZ(2484),		14,	FREQ_24,	},
	{ HMHZ(36575),		131,	FREQ_36,	},
	{ HMHZ(36625),		132,	FREQ_36,	},
	{ HMHZ(36600),		132,	FREQ_36,	},
	{ HMHZ(36650),		133,	FREQ_36,	},
	{ HMHZ(36675),		133,	FREQ_36,	},
	{ HMHZ(36700),		134,	FREQ_36,	},
	{ HMHZ(36725),		134,	FREQ_36,	},
	{ HMHZ(36775),		135,	FREQ_36,	},
	{ HMHZ(36800),		136,	FREQ_36,	},
	{ HMHZ(36825),		136,	FREQ_36,	},
	{ HMHZ(36850),		137,	FREQ_36,	},
	{ HMHZ(36875),		137,	FREQ_36,	},
	{ HMHZ(36895),		138,	FREQ_36,	},
	{ HMHZ(36900),		138,	FREQ_36,	},
	{ MHZ(4915),		183,	FREQ_5,	},
	{ MHZ(4920),		184,	FREQ_5,	},
	{ MHZ(4925),		185,	FREQ_5,	},
	{ MHZ(4935),		187,	FREQ_5,	},
	{ MHZ(4940),		188,	FREQ_5,	},
	{ MHZ(4945),		189,	FREQ_5,	},
	{ MHZ(4960),		192,	FREQ_5,	},
	{ MHZ(4980),		196,	FREQ_5,	},
	{ MHZ(5035),		7,	FREQ_5,	},
	{ MHZ(5040),		8,	FREQ_5,	},
	{ MHZ(5045),		9,	FREQ_5,	},
	{ MHZ(5055),		11,	FREQ_5,	},
	{ MHZ(5060),		12,	FREQ_5,	},
	{ MHZ(5080),		16,	FREQ_5,	},
	{ MHZ(5170),		34,	FREQ_5,	},
	{ MHZ(5180),		36,	FREQ_5,	},
	{ MHZ(5190),		38,	FREQ_5,	},
	{ MHZ(5200),		40,	FREQ_5,	},
	{ MHZ(5210),		42,	FREQ_5,	},
	{ MHZ(5220),		44,	FREQ_5,	},
	{ MHZ(5230),		46,	FREQ_5,	},
	{ MHZ(5240),		48,	FREQ_5,	},
	{ MHZ(5260),		52,	FREQ_5,	},
	{ MHZ(5280),		56,	FREQ_5,	},
	{ MHZ(5300),		60,	FREQ_5,	},
	{ MHZ(5320),		64,	FREQ_5,	},
	{ MHZ(5500),		100,	FREQ_5,	},
	{ MHZ(5520),		104,	FREQ_5,	},
	{ MHZ(5540),		108,	FREQ_5,	},
	{ MHZ(5560),		112,	FREQ_5,	},
	{ MHZ(5580),		116,	FREQ_5,	},
	{ MHZ(5600),		120,	FREQ_5,	},
	{ MHZ(5620),		124,	FREQ_5,	},
	{ MHZ(5640),		128,	FREQ_5,	},
	{ MHZ(5660),		132,	FREQ_5,	},
	{ MHZ(5680),		136,	FREQ_5,	},
	{ MHZ(5700),		140,	FREQ_5,	},
	{ MHZ(5745),		149,	FREQ_5,	},
	{ MHZ(5765),		153,	FREQ_5,	},
	{ MHZ(5785),		157,	FREQ_5,	},
	{ MHZ(5805),		161,	FREQ_5,	},
	{ MHZ(5825),		165,	FREQ_5,	},
};

unsigned wireless_freq_count(void){
	return sizeof(freqtable) / sizeof(*freqtable);
}

float wireless_freq_supported_byidx(const interface *i,unsigned idx){
	if(idx >= wireless_freq_count()){
		return 0;
	}
	switch(i->settings_valid){
		case SETTINGS_VALID_NL80211:
			return i->settings.nl80211.dBm[idx];
		case SETTINGS_VALID_WEXT:
			return i->settings.wext.dBm[idx];
		// shouldn't see anything but wireless here
		case SETTINGS_VALID_ETHTOOL: default:
			assert(0);
	}
}

unsigned wireless_freq_byidx(unsigned idx){
	if(idx >= wireless_freq_count()){
		return 0;
	}
	return freqtable[idx].hz;
}

int wireless_idx_byfreq(unsigned freq){
	int idx,lb,ub;

	lb = 0;
	ub = sizeof(freqtable) / sizeof(*freqtable);
	do{
		idx = (lb + ub) / 2;
		if(freqtable[idx].hz == freq){
			return idx;
		}else if(freqtable[idx].hz < freq){
			lb = idx + 1;
		}else{
			ub = idx - 1;
		}
	}while(lb < ub);
	return -1;	// invalid frequency!
}

unsigned wireless_chan_byidx(unsigned idx){
	if(idx >= wireless_freq_count()){
		return 0;
	}
	return freqtable[idx].channel;
}
