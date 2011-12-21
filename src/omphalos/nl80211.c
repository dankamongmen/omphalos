#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <omphalos/diag.h>
#include <linux/nl80211.h>
#include <linux/netlink.h>
#include <omphalos/util.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include <omphalos/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <omphalos/interface.h>
#include <netlink/genl/family.h>

static struct nl_sock *nl;
static struct nl_cache *nlc;
static struct genl_family *nl80211;
static pthread_mutex_t nllock = PTHREAD_MUTEX_INITIALIZER;

static const char *commands[NL80211_CMD_MAX + 1] = {
	[NL80211_CMD_GET_WIPHY] = "get_wiphy",
	[NL80211_CMD_SET_WIPHY] = "set_wiphy",
	[NL80211_CMD_NEW_WIPHY] = "new_wiphy",
	[NL80211_CMD_DEL_WIPHY] = "del_wiphy",
	[NL80211_CMD_GET_INTERFACE] = "get_interface",
	[NL80211_CMD_SET_INTERFACE] = "set_interface",
	[NL80211_CMD_NEW_INTERFACE] = "new_interface",
	[NL80211_CMD_DEL_INTERFACE] = "del_interface",
	[NL80211_CMD_GET_KEY] = "get_key",
	[NL80211_CMD_SET_KEY] = "set_key",
	[NL80211_CMD_NEW_KEY] = "new_key",
	[NL80211_CMD_DEL_KEY] = "del_key",
	[NL80211_CMD_GET_BEACON] = "get_beacon",
	[NL80211_CMD_SET_BEACON] = "set_beacon",
	[NL80211_CMD_NEW_BEACON] = "new_beacon",
	[NL80211_CMD_DEL_BEACON] = "del_beacon",
	[NL80211_CMD_GET_STATION] = "get_station",
	[NL80211_CMD_SET_STATION] = "set_station",
	[NL80211_CMD_NEW_STATION] = "new_station",
	[NL80211_CMD_DEL_STATION] = "del_station",
	[NL80211_CMD_GET_MPATH] = "get_mpath",
	[NL80211_CMD_SET_MPATH] = "set_mpath",
	[NL80211_CMD_NEW_MPATH] = "new_mpath",
	[NL80211_CMD_DEL_MPATH] = "del_mpath",
	[NL80211_CMD_SET_BSS] = "set_bss",
	[NL80211_CMD_SET_REG] = "set_reg",
	[NL80211_CMD_REQ_SET_REG] = "reg_set_reg",
	[NL80211_CMD_GET_MESH_PARAMS] = "get_mesh_params",
	[NL80211_CMD_SET_MESH_PARAMS] = "set_mesh_params",
	[NL80211_CMD_SET_MGMT_EXTRA_IE] = "set_mgmt_extra_ie",
	[NL80211_CMD_GET_REG] = "get_reg",
	[NL80211_CMD_GET_SCAN] = "get_scan",
	[NL80211_CMD_TRIGGER_SCAN] = "trigger_scan",
	[NL80211_CMD_NEW_SCAN_RESULTS] = "new_scan_results",
	[NL80211_CMD_SCAN_ABORTED] = "scan_aborted",
	[NL80211_CMD_REG_CHANGE] = "reg_change",
	[NL80211_CMD_AUTHENTICATE] = "authenticate",
	[NL80211_CMD_ASSOCIATE] = "associate",
	[NL80211_CMD_DEAUTHENTICATE] = "deauthenticate",
	[NL80211_CMD_DISASSOCIATE] = "disassociate",
	[NL80211_CMD_MICHAEL_MIC_FAILURE] = "michael_mic_failure",
	[NL80211_CMD_REG_BEACON_HINT] = "reg_beacon_hint",
	[NL80211_CMD_JOIN_IBSS] = "join_ibss",
	[NL80211_CMD_LEAVE_IBSS] = "leave_ibss",
	[NL80211_CMD_TESTMODE] = "testmode",
	[NL80211_CMD_CONNECT] = "connect",
	[NL80211_CMD_ROAM] = "roam",
	[NL80211_CMD_DISCONNECT] = "disconnect",
	[NL80211_CMD_SET_WIPHY_NETNS] = "set_wiphy_netns",
	[NL80211_CMD_GET_SURVEY] = "get_survey",
	[NL80211_CMD_SET_PMKSA] = "set_pmksa",
	[NL80211_CMD_DEL_PMKSA] = "del_pmksa",
	[NL80211_CMD_FLUSH_PMKSA] = "flush_pmksa",
	[NL80211_CMD_REMAIN_ON_CHANNEL] = "remain_on_channel",
	[NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL] = "cancel_remain_on_channel",
	[NL80211_CMD_SET_TX_BITRATE_MASK] = "set_tx_bitrate_mask",
	[NL80211_CMD_REGISTER_ACTION] = "register_action",
	[NL80211_CMD_ACTION] = "action",
	[NL80211_CMD_SET_CHANNEL] = "set_channel",
	[NL80211_CMD_SET_WDS_PEER] = "set_wds_peer",
	[NL80211_CMD_FRAME_WAIT_CANCEL] = "frame_wait_cancel",
	[NL80211_CMD_JOIN_MESH] = "join_mesh",
	[NL80211_CMD_LEAVE_MESH] = "leave_mesh",
	[NL80211_CMD_SET_REKEY_OFFLOAD] = "set_rekey_offload",
};

static char cmdbuf[100];

const char *command_name(enum nl80211_commands cmd)
{
	if (cmd <= NL80211_CMD_MAX && commands[cmd])
		return commands[cmd];
	sprintf(cmdbuf, "Unknown command (%d)", cmd);
	return cmdbuf;
}


static const char *ifmodes[NL80211_IFTYPE_MAX + 1] = {
	"unspecified",
	"IBSS",
	"managed",
	"AP",
	"AP/VLAN",
	"WDS",
	"monitor",
	"mesh point",
	"P2P-client",
	"P2P-GO",
};

static char modebuf[100];

const char *iftype_name(enum nl80211_iftype iftype)
{
	if (iftype <= NL80211_IFTYPE_MAX)
		return ifmodes[iftype];
	sprintf(modebuf, "Unknown mode (%d)", iftype);
	return modebuf;
}



#define BIT(n) (1 << n)

static void print_flag(const char *name, int *open)
{
	if (!*open)
		diagnostic(" (");
	else
		diagnostic(", ");
	diagnostic("%s", name);
	*open = 1;
}

int ieee80211_frequency_to_channel(int freq)
{
	if (freq == 2484)
		return 14;

	if (freq < 2484)
		return (freq - 2407) / 5;

	/* FIXME: dot11ChannelStartingFactor (802.11-2007 17.3.8.3.2) */
	return freq/5 - 1000;
}


static void print_mcs_index(const __u8 *mcs)
{
	int mcs_bit, prev_bit = -2, prev_cont = 0;

	for (mcs_bit = 0; mcs_bit <= 76; mcs_bit++) {
		unsigned int mcs_octet = mcs_bit/8;
		unsigned int MCS_RATE_BIT = 1 << mcs_bit % 8;
		bool mcs_rate_idx_set;

		mcs_rate_idx_set = !!(mcs[mcs_octet] & MCS_RATE_BIT);

		if (!mcs_rate_idx_set)
			continue;

		if (prev_bit != mcs_bit - 1) {
			if (prev_bit != -2)
				diagnostic("%d, ", prev_bit);
			else
				diagnostic(" ");
			diagnostic("%d", mcs_bit);
			prev_cont = 0;
		} else if (!prev_cont) {
			diagnostic("-");
			prev_cont = 1;
		}

		prev_bit = mcs_bit;
	}

	if (prev_cont)
		diagnostic("%d", prev_bit);
	diagnostic("");
}


/*
 * There are only 4 possible values, we just use a case instead of computing it,
 * but technically this can also be computed through the formula:
 *
 * Max AMPDU length = (2 ^ (13 + exponent)) - 1 bytes
 */
static __u32 compute_ampdu_length(__u8 exponent)
{
	switch (exponent) {
	case 0: return 8191;  /* (2 ^(13 + 0)) -1 */
	case 1: return 16383; /* (2 ^(13 + 1)) -1 */
	case 2: return 32767; /* (2 ^(13 + 2)) -1 */
	case 3: return 65535; /* (2 ^(13 + 3)) -1 */
	default: return 0;
	}
}

static const char *print_ampdu_space(__u8 space)
{
	switch (space) {
	case 0: return "No restriction";
	case 1: return "1/4 usec";
	case 2: return "1/2 usec";
	case 3: return "1 usec";
	case 4: return "2 usec";
	case 5: return "4 usec";
	case 6: return "8 usec";
	case 7: return "16 usec";
	default:
		return "BUG (spacing more than 3 bits!)";
	}
}

void print_ampdu_length(__u8 exponent)
{
	__u32 max_ampdu_length;

	max_ampdu_length = compute_ampdu_length(exponent);

	if (max_ampdu_length) {
		diagnostic("\t\tMaximum RX AMPDU length %d bytes (exponent: 0x0%02x)",
		       max_ampdu_length, exponent);
	} else {
		diagnostic("\t\tMaximum RX AMPDU length: unrecognized bytes "
		       "(exponent: %d)", exponent);
	}
}

void print_ampdu_spacing(__u8 spacing)
{
	diagnostic("\t\tMinimum RX AMPDU time spacing: %s (0x%02x)",
	       print_ampdu_space(spacing), spacing);
}

/*static void
print_ht_capability(__u16 cap){
#define PRINT_HT_CAP(_cond, _str) \
	do { \
		if (_cond) \
			diagnostic("\t\t\t" _str ""); \
	} while (0)

	diagnostic("\t\tCapabilities: 0x%02x", cap);

	PRINT_HT_CAP((cap & BIT(0)), "RX LDPC");
	PRINT_HT_CAP((cap & BIT(1)), "HT20/HT40");
	PRINT_HT_CAP(!(cap & BIT(1)), "HT20");

	PRINT_HT_CAP(((cap >> 2) & 0x3) == 0, "Static SM Power Save");
	PRINT_HT_CAP(((cap >> 2) & 0x3) == 1, "Dynamic SM Power Save");
	PRINT_HT_CAP(((cap >> 2) & 0x3) == 3, "SM Power Save disabled");

	PRINT_HT_CAP((cap & BIT(4)), "RX Greenfield");
	PRINT_HT_CAP((cap & BIT(5)), "RX HT20 SGI");
	PRINT_HT_CAP((cap & BIT(6)), "RX HT40 SGI");
	PRINT_HT_CAP((cap & BIT(7)), "TX STBC");

	PRINT_HT_CAP(((cap >> 8) & 0x3) == 0, "No RX STBC");
	PRINT_HT_CAP(((cap >> 8) & 0x3) == 1, "RX STBC 1-stream");
	PRINT_HT_CAP(((cap >> 8) & 0x3) == 2, "RX STBC 2-streams");
	PRINT_HT_CAP(((cap >> 8) & 0x3) == 3, "RX STBC 3-streams");

	PRINT_HT_CAP((cap & BIT(10)), "HT Delayed Block Ack");

	PRINT_HT_CAP(!(cap & BIT(11)), "Max AMSDU length: 3839 bytes");
	PRINT_HT_CAP((cap & BIT(11)), "Max AMSDU length: 7935 bytes");

	//
	// For beacons and probe response this would mean the BSS
	// does or does not allow the usage of DSSS/CCK HT40.
	// Otherwise it means the STA does or does not use
	// DSSS/CCK HT40.
	//
	PRINT_HT_CAP((cap & BIT(12)), "DSSS/CCK HT40");
	PRINT_HT_CAP(!(cap & BIT(12)), "No DSSS/CCK HT40");

	// BIT(13) is reserved

	PRINT_HT_CAP((cap & BIT(14)), "40 MHz Intolerant");

	PRINT_HT_CAP((cap & BIT(15)), "L-SIG TXOP protection");
#undef PRINT_HT_CAP
}*/

void print_ht_mcs(const __u8 *mcs)
{
	/* As defined in 7.3.2.57.4 Supported MCS Set field */
	unsigned int tx_max_num_spatial_streams, max_rx_supp_data_rate;
	bool tx_mcs_set_defined, tx_mcs_set_equal, tx_unequal_modulation;

	max_rx_supp_data_rate = ((mcs[10] >> 8) & ((mcs[11] & 0x3) << 8));
	tx_mcs_set_defined = !!(mcs[12] & (1 << 0));
	tx_mcs_set_equal = !(mcs[12] & (1 << 1));
	tx_max_num_spatial_streams = ((mcs[12] >> 2) & 3) + 1;
	tx_unequal_modulation = !!(mcs[12] & (1 << 4));

	if (max_rx_supp_data_rate)
		diagnostic("\t\tHT Max RX data rate: %d Mbps", max_rx_supp_data_rate);
	/* XXX: else see 9.6.0e.5.3 how to get this I think */

	if (tx_mcs_set_defined) {
		if (tx_mcs_set_equal) {
			diagnostic("\t\tHT TX/RX MCS rate indexes supported:");
			print_mcs_index(mcs);
		} else {
			diagnostic("\t\tHT RX MCS rate indexes supported:");
			print_mcs_index(mcs);

			if (tx_unequal_modulation)
				diagnostic("\t\tTX unequal modulation supported");
			else
				diagnostic("\t\tTX unequal modulation not supported");

			diagnostic("\t\tHT TX Max spatial streams: %d",
				tx_max_num_spatial_streams);

			diagnostic("\t\tHT TX MCS rate indexes supported may differ");
		}
	} else {
		diagnostic("\t\tHT RX MCS rate indexes supported:");
		print_mcs_index(mcs);
		diagnostic("\t\tHT TX MCS rate indexes are undefined");
	}
}


static int
error_handler(struct sockaddr_nl *nla __attribute__ ((unused)),
				struct nlmsgerr *err,void *arg){
	int *ret = arg;
	*ret = err->error;
	assert(0);
	return NL_STOP;
}

static int
finish_handler(struct nl_msg *msg __attribute__ ((unused)),void *arg){
	int *ret = arg;
	*ret = 1;
	return NL_SKIP;
}

static int
ack_handler(struct nl_msg *msg __attribute__ ((unused)),void *arg){
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

static int
phy_handler(struct nl_msg *msg,void *arg __attribute__ ((unused))){
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	struct nlattr *tb_band[NL80211_BAND_ATTR_MAX + 1];

	struct nlattr *tb_freq[NL80211_FREQUENCY_ATTR_MAX + 1];
	static struct nla_policy freq_policy[NL80211_FREQUENCY_ATTR_MAX + 1] = {
		[NL80211_FREQUENCY_ATTR_FREQ] = { .type = NLA_U32 },
		[NL80211_FREQUENCY_ATTR_DISABLED] = { .type = NLA_FLAG },
		[NL80211_FREQUENCY_ATTR_PASSIVE_SCAN] = { .type = NLA_FLAG },
		[NL80211_FREQUENCY_ATTR_NO_IBSS] = { .type = NLA_FLAG },
		[NL80211_FREQUENCY_ATTR_RADAR] = { .type = NLA_FLAG },
		[NL80211_FREQUENCY_ATTR_MAX_TX_POWER] = { .type = NLA_U32 },
	};

	//struct nlattr *tb_rate[NL80211_BITRATE_ATTR_MAX + 1];
	/*static struct nla_policy rate_policy[NL80211_BITRATE_ATTR_MAX + 1] = {
		[NL80211_BITRATE_ATTR_RATE] = { .type = NLA_U32 },
		[NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE] = { .type = NLA_FLAG },
	};*/

	struct nlattr *nl_band;
	struct nlattr *nl_freq;
	//struct nlattr *nl_rate;
	//struct nlattr *nl_mode;
	//struct nlattr *nl_cmd;
	//struct nlattr *nl_if, *nl_ftype;
	int bandidx = 1;
	int rem_band, rem_freq;//, rem_rate, rem_mode, rem_cmd, rem_ftype, rem_if;
	int open;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb_msg[NL80211_ATTR_WIPHY_BANDS])
		return NL_SKIP;

	if (tb_msg[NL80211_ATTR_WIPHY_NAME])
		diagnostic("Wiphy %s", nla_get_string(tb_msg[NL80211_ATTR_WIPHY_NAME]));
	
	nla_for_each_nested(nl_band, tb_msg[NL80211_ATTR_WIPHY_BANDS], rem_band) {
		diagnostic("\tBand %d:", bandidx);
		bandidx++;

		nla_parse(tb_band, NL80211_BAND_ATTR_MAX, nla_data(nl_band),
			  nla_len(nl_band), NULL);

		/*
#ifdef NL80211_BAND_ATTR_HT_CAPA
		if (tb_band[NL80211_BAND_ATTR_HT_CAPA]) {
			__u16 cap = nla_get_u16(tb_band[NL80211_BAND_ATTR_HT_CAPA]);
			print_ht_capability(cap);
		}
		if (tb_band[NL80211_BAND_ATTR_HT_AMPDU_FACTOR]) {
			__u8 exponent = nla_get_u8(tb_band[NL80211_BAND_ATTR_HT_AMPDU_FACTOR]);
			print_ampdu_length(exponent);
		}
		if (tb_band[NL80211_BAND_ATTR_HT_AMPDU_DENSITY]) {
			__u8 spacing = nla_get_u8(tb_band[NL80211_BAND_ATTR_HT_AMPDU_DENSITY]);
			print_ampdu_spacing(spacing);
		}
		if (tb_band[NL80211_BAND_ATTR_HT_MCS_SET] &&
		    nla_len(tb_band[NL80211_BAND_ATTR_HT_MCS_SET]) == 16)
			print_ht_mcs(nla_data(tb_band[NL80211_BAND_ATTR_HT_MCS_SET]));
#endif
		*/

		diagnostic("\t\tFrequencies:");

		nla_for_each_nested(nl_freq, tb_band[NL80211_BAND_ATTR_FREQS], rem_freq) {
			uint32_t freq;
			int chan;

			nla_parse(tb_freq, NL80211_FREQUENCY_ATTR_MAX, nla_data(nl_freq),
				  nla_len(nl_freq), freq_policy);
			if (!tb_freq[NL80211_FREQUENCY_ATTR_FREQ])
				continue;
			freq = nla_get_u32(tb_freq[NL80211_FREQUENCY_ATTR_FREQ]);
			chan = ieee80211_frequency_to_channel(freq);
			if(chan > MAX_WIRELESS_CHANNEL){
				goto err;
			}
			diagnostic("\t\t\t* %d MHz [%d]", freq, ieee80211_frequency_to_channel(freq));

			if (tb_freq[NL80211_FREQUENCY_ATTR_MAX_TX_POWER] &&
			    !tb_freq[NL80211_FREQUENCY_ATTR_DISABLED])
				diagnostic(" (%.1f dBm)", 0.01 * nla_get_u32(tb_freq[NL80211_FREQUENCY_ATTR_MAX_TX_POWER]));

			open = 0;
			if (tb_freq[NL80211_FREQUENCY_ATTR_DISABLED]) {
				print_flag("disabled", &open);
				goto next;
			}
			if (tb_freq[NL80211_FREQUENCY_ATTR_PASSIVE_SCAN])
				print_flag("passive scanning", &open);
			if (tb_freq[NL80211_FREQUENCY_ATTR_NO_IBSS])
				print_flag("no IBSS", &open);
			if (tb_freq[NL80211_FREQUENCY_ATTR_RADAR])
				print_flag("radar detection", &open);
 next:
			if (open)
				diagnostic(")");
			diagnostic("");
		}

		/*
		diagnostic("\t\tBitrates (non-HT):");

		nla_for_each_nested(nl_rate, tb_band[NL80211_BAND_ATTR_RATES], rem_rate) {
			nla_parse(tb_rate, NL80211_BITRATE_ATTR_MAX, nla_data(nl_rate),
				  nla_len(nl_rate), rate_policy);
			if (!tb_rate[NL80211_BITRATE_ATTR_RATE])
				continue;
			diagnostic("\t\t\t* %2.1f Mbps", 0.1 * nla_get_u32(tb_rate[NL80211_BITRATE_ATTR_RATE]));
			open = 0;
			if (tb_rate[NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE])
				print_flag("short preamble supported", &open);
			if (open)
				diagnostic(")");
			diagnostic("");
		}
		*/
	}

	/*
	if (tb_msg[NL80211_ATTR_MAX_NUM_SCAN_SSIDS])
		diagnostic("\tmax # scan SSIDs: %d",
		       nla_get_u8(tb_msg[NL80211_ATTR_MAX_NUM_SCAN_SSIDS]));
	if (tb_msg[NL80211_ATTR_MAX_SCAN_IE_LEN])
		diagnostic("\tmax scan IEs length: %d bytes",
		       nla_get_u16(tb_msg[NL80211_ATTR_MAX_SCAN_IE_LEN]));

	if (tb_msg[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]) {
		unsigned int frag;

		frag = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]);
		if (frag != (unsigned int)-1)
			diagnostic("\tFragmentation threshold: %d", frag);
	}

	if (tb_msg[NL80211_ATTR_WIPHY_RTS_THRESHOLD]) {
		unsigned int rts;

		rts = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_RTS_THRESHOLD]);
		if (rts != (unsigned int)-1)
			diagnostic("\tRTS threshold: %d", rts);
	}

	if (tb_msg[NL80211_ATTR_WIPHY_COVERAGE_CLASS]) {
		unsigned char coverage;

		coverage = nla_get_u8(tb_msg[NL80211_ATTR_WIPHY_COVERAGE_CLASS]);
		// See handle_distance() for an explanation where the '450' comes from
		diagnostic("\tCoverage class: %d (up to %dm)", coverage, 450 * coverage);
	}

	if (tb_msg[NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX] &&
	    tb_msg[NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX])
		diagnostic("\tAvailable Antennas: TX %#x RX %#x",
		       nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX]),
		       nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX]));

	if (tb_msg[NL80211_ATTR_WIPHY_ANTENNA_TX] &&
	    tb_msg[NL80211_ATTR_WIPHY_ANTENNA_RX])
		diagnostic("\tConfigured Antennas: TX %#x RX %#x",
		       nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_ANTENNA_TX]),
		       nla_get_u32(tb_msg[NL80211_ATTR_WIPHY_ANTENNA_RX]));

	if (tb_msg[NL80211_ATTR_SUPPORTED_IFTYPES]) {
		diagnostic("\tSupported interface modes:");
		nla_for_each_nested(nl_mode, tb_msg[NL80211_ATTR_SUPPORTED_IFTYPES], rem_mode)
			diagnostic("\t\t * %s", iftype_name(nla_type(nl_mode)));
	}

	if (tb_msg[NL80211_ATTR_SOFTWARE_IFTYPES]) {
		diagnostic("\tsoftware interface modes (can always be added):");
		nla_for_each_nested(nl_mode, tb_msg[NL80211_ATTR_SOFTWARE_IFTYPES], rem_mode)
			diagnostic("\t\t * %s", iftype_name(nla_type(nl_mode)));
	}

	if (tb_msg[NL80211_ATTR_INTERFACE_COMBINATIONS]) {
		struct nlattr *nl_combi;
		int rem_combi;
		bool have_combinations = false;

		nla_for_each_nested(nl_combi, tb_msg[NL80211_ATTR_INTERFACE_COMBINATIONS], rem_combi) {
			static struct nla_policy iface_combination_policy[NUM_NL80211_IFACE_COMB] = {
				[NL80211_IFACE_COMB_LIMITS] = { .type = NLA_NESTED },
				[NL80211_IFACE_COMB_MAXNUM] = { .type = NLA_U32 },
				[NL80211_IFACE_COMB_STA_AP_BI_MATCH] = { .type = NLA_FLAG },
				[NL80211_IFACE_COMB_NUM_CHANNELS] = { .type = NLA_U32 },
			};
			struct nlattr *tb_comb[NUM_NL80211_IFACE_COMB];
			static struct nla_policy iface_limit_policy[NUM_NL80211_IFACE_LIMIT] = {
				[NL80211_IFACE_LIMIT_TYPES] = { .type = NLA_NESTED },
				[NL80211_IFACE_LIMIT_MAX] = { .type = NLA_U32 },
			};
			struct nlattr *tb_limit[NUM_NL80211_IFACE_LIMIT];
			struct nlattr *nl_limit;
			int err, rem_limit;
			bool comma = false;

			if (!have_combinations) {
				diagnostic("\tvalid interface combinations:");
				have_combinations = true;
			}

			diagnostic("\t\t * ");

			err = nla_parse_nested(tb_comb, MAX_NL80211_IFACE_COMB,
					       nl_combi, iface_combination_policy);
			if (err || !tb_comb[NL80211_IFACE_COMB_LIMITS] ||
			    !tb_comb[NL80211_IFACE_COMB_MAXNUM] ||
			    !tb_comb[NL80211_IFACE_COMB_NUM_CHANNELS]) {
				diagnostic(" <failed to parse>");
				goto broken_combination;
			}

			nla_for_each_nested(nl_limit, tb_comb[NL80211_IFACE_COMB_LIMITS], rem_limit) {
				bool ift_comma = false;

				err = nla_parse_nested(tb_limit, MAX_NL80211_IFACE_LIMIT,
						       nl_limit, iface_limit_policy);
				if (err || !tb_limit[NL80211_IFACE_LIMIT_TYPES]) {
					diagnostic("<failed to parse>");
					goto broken_combination;
				}

				if (comma)
					diagnostic(", ");
				comma = true;
				diagnostic("#{");

				nla_for_each_nested(nl_mode, tb_limit[NL80211_IFACE_LIMIT_TYPES], rem_mode) {
					diagnostic("%s %s", ift_comma ? "," : "",
						iftype_name(nla_type(nl_mode)));
					ift_comma = true;
				}
				diagnostic(" } <= %u", nla_get_u32(tb_limit[NL80211_IFACE_LIMIT_MAX]));
			}
			diagnostic(",\t\t   ");

			diagnostic("total <= %d, #channels <= %d%s",
				nla_get_u32(tb_comb[NL80211_IFACE_COMB_MAXNUM]),
				nla_get_u32(tb_comb[NL80211_IFACE_COMB_NUM_CHANNELS]),
				tb_comb[NL80211_IFACE_COMB_STA_AP_BI_MATCH] ?
					", STA/AP BI must match" : "");
broken_combination:
			;
		}

		if (!have_combinations)
			diagnostic("\tinterface combinations are not supported");
	}

	if (tb_msg[NL80211_ATTR_SUPPORTED_COMMANDS]) {
		diagnostic("\tSupported commands:");
		nla_for_each_nested(nl_cmd, tb_msg[NL80211_ATTR_SUPPORTED_COMMANDS], rem_cmd)
			diagnostic("\t\t * %s", command_name(nla_get_u32(nl_cmd)));
	}

	if (tb_msg[NL80211_ATTR_TX_FRAME_TYPES]) {
		diagnostic("\tSupported TX frame types:");
		nla_for_each_nested(nl_if, tb_msg[NL80211_ATTR_TX_FRAME_TYPES], rem_if) {
			bool printed = false;
			nla_for_each_nested(nl_ftype, nl_if, rem_ftype) {
				if (!printed)
					diagnostic("\t\t * %s:", iftype_name(nla_type(nl_if)));
				printed = true;
				diagnostic(" 0x%.4x", nla_get_u16(nl_ftype));
			}
			if (printed)
				diagnostic("");
		}
	}

	if (tb_msg[NL80211_ATTR_RX_FRAME_TYPES]) {
		diagnostic("\tSupported RX frame types:");
		nla_for_each_nested(nl_if, tb_msg[NL80211_ATTR_RX_FRAME_TYPES], rem_if) {
			bool printed = false;
			nla_for_each_nested(nl_ftype, nl_if, rem_ftype) {
				if (!printed)
					diagnostic("\t\t * %s:", iftype_name(nla_type(nl_if)));
				printed = true;
				diagnostic(" 0x%.4x", nla_get_u16(nl_ftype));
			}
			if (printed)
				diagnostic("");
		}
	}

	if (tb_msg[NL80211_ATTR_SUPPORT_IBSS_RSN])
		diagnostic("\tDevice supports RSN-IBSS.");

	if (tb_msg[NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED]) {
		struct nlattr *tb_wowlan[NUM_NL80211_WOWLAN_TRIG];
		static struct nla_policy wowlan_policy[NUM_NL80211_WOWLAN_TRIG] = {
			[NL80211_WOWLAN_TRIG_ANY] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_DISCONNECT] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_MAGIC_PKT] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_PKT_PATTERN] = {
				.minlen = sizeof(struct nl80211_wowlan_pattern_support),
			},
			[NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE] = { .type = NLA_FLAG },
			[NL80211_WOWLAN_TRIG_RFKILL_RELEASE] = { .type = NLA_FLAG },
		};
		struct nl80211_wowlan_pattern_support *pat;
		int err;

		err = nla_parse_nested(tb_wowlan, MAX_NL80211_WOWLAN_TRIG,
				       tb_msg[NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED],
				       wowlan_policy);
		diagnostic("\tWoWLAN support:");
		if (err) {
			diagnostic(" <failed to parse>");
		} else {
			diagnostic("");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_ANY])
				diagnostic("\t\t * wake up on anything (device continues operating normally)");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_DISCONNECT])
				diagnostic("\t\t * wake up on disconnect");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_MAGIC_PKT])
				diagnostic("\t\t * wake up on magic packet");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_PKT_PATTERN]) {
				pat = nla_data(tb_wowlan[NL80211_WOWLAN_TRIG_PKT_PATTERN]);
				diagnostic("\t\t * wake up on pattern match, up to %u patterns of %u-%u bytes",
					pat->max_patterns, pat->min_pattern_len, pat->max_pattern_len);
			}
			if (tb_wowlan[NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED])
				diagnostic("\t\t * can do GTK rekeying");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE])
				diagnostic("\t\t * wake up on GTK rekey failure");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST])
				diagnostic("\t\t * wake up on EAP identity request");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE])
				diagnostic("\t\t * wake up on 4-way handshake");
			if (tb_wowlan[NL80211_WOWLAN_TRIG_RFKILL_RELEASE])
				diagnostic("\t\t * wake up on rfkill release");
		}
	}
	*/
	return NL_SKIP;

err:
	return NL_SKIP;
}

static int
dev_handler(struct nl_msg *msg,void *arg __attribute__ ((unused))){
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
        struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	unsigned wiphy;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);
	wiphy = nla_get_u32(tb_msg[NL80211_ATTR_WIPHY]);
	diagnostic("PHY %u %s %d",wiphy,
			nla_get_string(tb_msg[NL80211_ATTR_IFNAME]),
			nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]));
	return NL_SKIP;
}

static int
nl80211_cmd(enum nl80211_commands cmd,int flags,int(*handler)(struct nl_msg *,void *),
						int attr,uint32_t arg){
	struct nl_cb *cb = NULL,*scb = NULL;
	struct nl_msg *msg;
	int err;

	if((msg = nlmsg_alloc()) == NULL){
		diagnostic("Couldn't allocate netlink msg (%s?)",strerror(errno));
		return -1;
	}
	if((cb = nl_cb_alloc(NL_CB_VERBOSE)) == NULL){
		diagnostic("Couldn't allocate netlink cb (%s?)",strerror(errno));
		goto err;
	}
	if((scb = nl_cb_alloc(NL_CB_VERBOSE)) == NULL){
		diagnostic("Couldn't allocate netlink cb (%s?)",strerror(errno));
		goto err;
	}
	genlmsg_put(msg,0,0,genl_family_get_id(nl80211),0,flags,cmd,0);
	NLA_PUT_U32(msg,attr,arg);
	nl_cb_set(cb,NL_CB_VALID,NL_CB_CUSTOM,handler,&err);
	nl_socket_set_cb(nl,scb);
	if(nl_send_auto_complete(nl,msg) < 0){
		diagnostic("Couldn't send msg (%s?)",strerror(errno));
		goto err;
	}
	nl_cb_err(cb,NL_CB_CUSTOM,error_handler,&err);
	nl_cb_set(cb,NL_CB_FINISH,NL_CB_CUSTOM,finish_handler,&err);
	nl_cb_set(cb,NL_CB_ACK,NL_CB_CUSTOM,ack_handler,&err);
	err = 0;
	while(!err){
		nl_recvmsgs(nl,cb);
	}
	nl_cb_put(scb);
	nl_cb_put(cb);
	nlmsg_free(msg);
	return 0;

nla_put_failure: // part of NLA_PUT_*() interface
err:
	if(cb){
		nl_cb_put(cb);
	}
	if(scb){
		nl_cb_put(scb);
	}
	nlmsg_free(msg);
	return -1;
}

int open_nl80211(void){
	assert(pthread_mutex_lock(&nllock) == 0);
	if(nl){ // already initialized
		pthread_mutex_unlock(&nllock);
		return 0;
	}
	if((nl = nl_socket_alloc()) == NULL){
		diagnostic("Couldn't allocate generic netlink (%s?)",strerror(errno));
		goto err;
	}
	if(genl_connect(nl)){
		diagnostic("Couldn't connect generic netlink (%s?)",strerror(errno));
		goto err;
	}
	if(genl_ctrl_alloc_cache(nl,&nlc)){
		diagnostic("Couldn't allocate netlink cache (%s?)",strerror(errno));
		goto err;
	}
	if((nl80211 = genl_ctrl_search_by_name(nlc,"nl80211")) == NULL){
		//diagnostic("Couldn't find nl80211 (%s?)",strerror(errno));
		goto err;
	}
	// FIXME extract these from the netlink layer; they're sent back to us
	// in the first message. there's also "regulatory" and "scan".
	/*nl_get_multicast_id(nl,"nl80211","mlme");
	nl_socket_add_membership(
	nl_get_multicast_id(nl,"nl80211","config");*/
	assert(pthread_mutex_unlock(&nllock) == 0);
	return 0;

err:
	if(nlc){
		nl_cache_free(nlc);
		nlc = NULL;
	}
	if(nl){
		nl_socket_free(nl);
		nl = NULL;
	}
	assert(pthread_mutex_unlock(&nllock) == 0);
	return -1;
}

int close_nl80211(void){
	assert(pthread_mutex_lock(&nllock) == 0);
	if(!nl){ // never constructed, or already destroyed
		assert(pthread_mutex_unlock(&nllock) == 0);
		return 0;
	}
	nl_cache_free(nlc);
	nl_socket_free(nl);
	assert(pthread_mutex_unlock(&nllock) == 0);
	return 0;
}

int iface_nl80211_info(const interface *i,nl80211_info *nl){
	int idx;

	Pthread_mutex_lock(&nllock);
	if(!nl){
		Pthread_mutex_unlock(&nllock);
		return -1;
	}
	memset(nl,0,sizeof(*nl));
	idx = idx_of_iface(i);
	if(nl80211_cmd(NL80211_CMD_GET_WIPHY,NLM_F_DUMP,phy_handler,
				NL80211_ATTR_WIPHY,0)){
		Pthread_mutex_unlock(&nllock);
		return -1;
	}
	if(nl80211_cmd(NL80211_CMD_GET_INTERFACE,NLM_F_DUMP,dev_handler,
					NL80211_ATTR_IFINDEX,idx)){
		Pthread_mutex_unlock(&nllock);
		return -1;
	}
	Pthread_mutex_unlock(&nllock);
	return 0;
}
