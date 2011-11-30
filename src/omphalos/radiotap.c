#include <limits.h>
#include <assert.h>
#include <sys/socket.h>
#include <linux/if_arp.h>
#include <omphalos/csum.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME i guess we need a copy of the upstream, since linux doesn't
// seem to install it? dubious, very dubious...
typedef struct radiotaphdr {
	uint8_t version;
	uint8_t pad;
	uint16_t len;
	uint32_t present;
} __attribute__ ((packed)) radiotaphdr;

/*struct {
	unsigned version: 2;
	unsigned type: 2;
	unsigned subtype: 4;
	unsigned tods: 1;
	unsigned fromds: 1;
	unsigned morefrag: 1;
	unsigned retry: 1;
	unsigned pwrmgmt: 1;
	unsigned moredata: 1;
	unsigned wep: 1;
	unsigned order: 1;
} control __attribute__ ((packed));*/

// This is all you get in the lowest common denominator (control frame). Note
// that data frames have bssid first, rather than dest, so you can't rely on
// that field without checking the type.
typedef struct ieee80211hdr {
	uint16_t control;
	uint16_t duration;
} __attribute__ ((packed)) ieee80211hdr;

typedef struct ieee80211ctrl {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
} __attribute__ ((packed)) ieee80211ctrl;

// IEEE 802.11 data encapsulation.
typedef struct ieee80211data {
	uint16_t control;
	uint16_t duration;
	unsigned char bssid[ETH_ALEN];
	unsigned char h_src[ETH_ALEN];
	unsigned char h_dest[ETH_ALEN];
	uint16_t fraqseq;
} __attribute__ ((packed)) ieee80211data;

// IEEE 802.11 management data. Followed by an IEEE 802.11 management frame.
typedef struct ieee80211mgmtdata {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_src[ETH_ALEN];
	unsigned char bssid[ETH_ALEN];
	uint16_t fraqseq;
} __attribute__ ((packed)) ieee80211mgmtdata;

// Fixed portion (12 bytes) of an IEEE 802.11 management frame. Followed by a
// tagged variable-length portion.
typedef struct ieee80211mgmt {
	uint64_t timestamp;
	uint16_t interval;
	uint16_t capabilities;
} __attribute__ ((packed)) ieee80211mgmt;

#define MANAGEMENT_FRAME			0
#define IEEE80211_SUBTYPE_PROBE_REQUEST		4
#define IEEE80211_SUBTYPE_PROBE_RESPONSE	5
#define IEEE80211_SUBTYPE_BEACON		8
#define CONTROL_FRAME				1
#define DATA_FRAME				2

#define IEEE80211_VERSION(ctrl) ((ntohs(ctrl) & 0x0300) >> 8u)
#define IEEE80211_TYPE(ctrl) ((ntohs(ctrl) & 0x0c00) >> 10u)
#define IEEE80211_SUBTYPE(ctrl) ((ntohs(ctrl) & 0xf000) >> 12u)

#define IEEE80211_MGMT_TAG_SSID		0
#define IEEE80211_MGMT_TAG_RATES	1

static void
handle_ieee80211_mgmtfix(omphalos_packet *op,const void *frame,size_t len,unsigned freq){
	const ieee80211mgmt *imgmt = frame;
	struct {
		void *ptr;
		size_t len;
	} tagtbl[1u << CHAR_BIT] = {};
	const unsigned char *tags;
	unsigned z;

	if(len < sizeof(*imgmt)){
		op->malformed = 1;
		diagnostic("%s mgmt frame too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	tags = (const unsigned char *)(imgmt + 1);
	len -= sizeof(*imgmt);
	// 1-byte tag number, 1-byte tag length, variable-length tag
	while(len > 1){
		unsigned taglen = tags[1];
		unsigned tag = tags[0];

		if(len < 2 + taglen){
			diagnostic("%s bad mgmt taglen (%zu/%u) on %s",
					__func__,len,taglen,op->i->name);
			break;
		}
		if(tagtbl[tag].ptr){
			// Tags can be duplicated, especially the Vendor
			// Specific tag (221)...handle that later FIXME
			len -= 2 + taglen;
			tags += 2 + taglen;
			continue;
		}
		tagtbl[tag].ptr = memdup(tags + 2,taglen);
		tagtbl[tag].len = taglen;
		len -= 2 + taglen;
		tags += 2 + taglen;
	}
	if(len){
		if(len < 2){
			diagnostic("%s bad mgmt tags (%zu) on %s",
					__func__,len,op->i->name);
		}
		op->malformed = 1;
		goto freetags;
	}
	if(tagtbl[IEEE80211_MGMT_TAG_SSID].ptr){
		char *tmp;

		// 8 for 16-bit frequency + unit plus space
		if((tmp = realloc(tagtbl[IEEE80211_MGMT_TAG_SSID].ptr,8 + tagtbl[IEEE80211_MGMT_TAG_SSID].len + 1)) == NULL){
			goto freetags;
		}
		tagtbl[IEEE80211_MGMT_TAG_SSID].ptr = tmp;
		// FIXME ugh
		snprintf(tmp + tagtbl[IEEE80211_MGMT_TAG_SSID].len,9," %u.%02uGHz",freq / 1000,(freq % 1000) / 10);
		name_l3host_absolute(op->i,op->l2s,op->l3s,tmp,NAMING_LEVEL_MAX);
	}

freetags:
	for(z = 0 ; z < sizeof(tagtbl) / sizeof(*tagtbl) ; ++z){
		free(tagtbl[z].ptr);
	}
}

static void
handle_ieee80211_mgmt(omphalos_packet *op,const void *frame,size_t len,unsigned freq){
	const ieee80211mgmtdata *ibec = frame;

	if(len < sizeof(*ibec)){
		op->malformed = 1;
		diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	op->l2s = lookup_l2host(op->i,ibec->h_src);
       	len -= sizeof(*ibec);
	op->l3s = lookup_local_l3host(&op->tv,op->i,op->l2s,AF_BSSID,ibec->bssid);
	handle_ieee80211_mgmtfix(op,(const char *)frame + sizeof(*ibec),len,freq);
}

static void
handle_ieee80211_ctrl(omphalos_packet *op,const void *frame,size_t len){
	const ieee80211ctrl *ictrl = frame;

	if(len < sizeof(*ictrl)){
		op->malformed = 1;
		diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	op->l2d = lookup_l2host(op->i,ictrl->h_dest);
	len -= sizeof(*ictrl);
}

static void
handle_ieee80211_data(omphalos_packet *op,const void *frame,size_t len){
	const ieee80211data *idata = frame;

	if(len < sizeof(*idata)){
		op->malformed = 1;
		diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	op->l2d = lookup_l2host(op->i,idata->h_dest);
	op->l2s = lookup_l2host(op->i,idata->h_src);
       	len -= sizeof(*idata);
	// FIXME and do what??
}

static void
handle_ieee80211_packet(omphalos_packet *op,const void *frame,size_t len,unsigned freq){
	const ieee80211hdr *ihdr = frame;

	// FIXME certain packets don't have the full 802.11 header (8 bytes,
	// control/duration/h_dest, seems to be the minimum).
	if(len < sizeof(ieee80211hdr)){
		op->malformed = 1;
		diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	if(IEEE80211_VERSION(ihdr->control) != 0){
		op->noproto = 1;
		diagnostic("%s Unknown version (%d) on %s",__func__,
				IEEE80211_VERSION(ihdr->control),op->i->name);
		return;
	}
	switch(IEEE80211_TYPE(ihdr->control)){
		case MANAGEMENT_FRAME:{
			unsigned stype = IEEE80211_SUBTYPE(ihdr->control);

			if(stype != IEEE80211_SUBTYPE_PROBE_REQUEST){
				handle_ieee80211_mgmt(op,frame,len,freq);
			}
		}break;
		case CONTROL_FRAME:{
			handle_ieee80211_ctrl(op,frame,len);
		}break;
		case DATA_FRAME:{
			handle_ieee80211_data(op,frame,len);
		}break;
		default:{
			op->noproto = 1;
			diagnostic("%s Unknown type %d on %s",__func__,
					IEEE80211_TYPE(ihdr->control),op->i->name);
			return;
		}break;
	}
}

enum ieee80211_radiotap_type {
	IEEE80211_RADIOTAP_TSFT = 0,
	IEEE80211_RADIOTAP_FLAGS = 1,
	IEEE80211_RADIOTAP_RATE = 2,
	IEEE80211_RADIOTAP_CHANNEL = 3,
	IEEE80211_RADIOTAP_FHSS = 4,
	IEEE80211_RADIOTAP_DBM_ANTSIGNAL = 5,
	IEEE80211_RADIOTAP_DBM_ANTNOISE = 6,
	IEEE80211_RADIOTAP_LOCK_QUALITY = 7,
	IEEE80211_RADIOTAP_TX_ATTENUATION = 8,
	IEEE80211_RADIOTAP_DB_TX_ATTENUATION = 9,
	IEEE80211_RADIOTAP_DBM_TX_POWER = 10,
	IEEE80211_RADIOTAP_ANTENNA = 11,
	IEEE80211_RADIOTAP_DB_ANTSIGNAL = 12,
	IEEE80211_RADIOTAP_DB_ANTNOISE = 13,
	IEEE80211_RADIOTAP_RX_FLAGS = 14,
	IEEE80211_RADIOTAP_TX_FLAGS = 15,
	IEEE80211_RADIOTAP_RTS_RETRIES = 16,
	IEEE80211_RADIOTAP_DATA_RETRIES = 17,
	IEEE80211_RADIOTAP_EXT = 31
};

/* For IEEE80211_RADIOTAP_FLAGS */
#define IEEE80211_RADIOTAP_F_CFP	0x01    /* sent/received
	                                         * during CFP
	                                         */
#define IEEE80211_RADIOTAP_F_SHORTPRE   0x02    /* sent/received
	                                         * with short
	                                         * preamble
	                                         */
#define IEEE80211_RADIOTAP_F_WEP	0x04    /* sent/received
	                                         * with WEP encryption
	                                         */
#define IEEE80211_RADIOTAP_F_FRAG       0x08    /* sent/received
	                                         * with fragmentation
	                                         */
#define IEEE80211_RADIOTAP_F_FCS	0x10    /* frame includes FCS */
#define IEEE80211_RADIOTAP_F_DATAPAD    0x20    /* frame has padding between
	                                         * 802.11 header and payload
	                                         * (to 32-bit boundary)
	                                         */

void handle_radiotap_packet(omphalos_packet *op,const void *frame,size_t len){
	const radiotaphdr *rhdr = frame;
	const void *ehdr,*vhdr;
	unsigned rlen,alignbit;
	size_t olen = len;
	uint32_t pres;
	uint16_t freq;

	// FIXME certain packets don't have the full 802.11 header (8 bytes,
	// control/duration/h_dest, seems to be the minimum).
	if(len < sizeof(radiotaphdr)){
		op->malformed = 1;
		diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	if(rhdr->version != 0){
		op->noproto = 1;
		diagnostic("%s Unknown radiotap version %d on %s",
				__func__,rhdr->version,op->i->name);
		return;
	}
	rlen = rhdr->len;
	if(len < rlen){
		op->malformed = 1;
		diagnostic("%s Radiotap too small (%zu < %u) on %s",
				__func__,len,rlen,op->i->name);
		return;
	}
	len -= rlen;
	ehdr = (const char *)frame + rlen;
	pres = rhdr->present;
	vhdr = (const char *)frame + sizeof(*rhdr);
	if(pres & (1u << IEEE80211_RADIOTAP_TSFT)){
		if(rlen < 8){
			goto malformed;
		}
		vhdr += 8;
		rlen -= 8;
		// preserves 64-bit alignment
	}
	// How many bits off are we from 64-bit alignment, assuming the
	// beginning is so aligned?
	alignbit = 0;
	if(pres & (1u << IEEE80211_RADIOTAP_FLAGS)){
		uint8_t flags;
		if(rlen < 1){
			goto malformed;
		}
		flags = *(const uint8_t *)vhdr;
		++vhdr;
		// There's a 32-bit FCS on the end built into the length here,
		// if the FCS bit is set in Flags. This affects actual 'len'.
		if(flags & IEEE80211_RADIOTAP_F_FCS){
			uint32_t fcs,cfcs;

			if(len < 4){
				goto malformed;
			}
			len -= 4;
			fcs = *(uint32_t *)((const char *)frame + (olen - 4));
			if((cfcs = ieee80211_fcs(ehdr,len)) != fcs){
				//diagnostic("%s Incorrect FCS (0x%08x vs 0x%08x) on %s",
				//		__func__,cfcs,fcs,op->i->name);
				op->malformed = 1;
				return;
			}
		}
		--rlen;
		alignbit = 8;
	}
	if(pres & (1u << IEEE80211_RADIOTAP_RATE)){
		if(rlen < 1){
			goto malformed;
		}
		++vhdr;
		--rlen;
		alignbit = alignbit + 8 % 64;
	}
	if(pres & (1u << IEEE80211_RADIOTAP_CHANNEL)){
		// uint16_t flags;
		if(alignbit % 16){
			if(rlen < 1){
				goto malformed;
			}
			++vhdr;
			--rlen;
			alignbit = alignbit + 8 % 64;
		}
		if(rlen < 4){
			goto malformed;
		}
		freq = *(const uint16_t *)vhdr;
		// flags = *((const uint16_t *)vhdr + 1);
		rlen -= 4;
		vhdr += 4;
	}else{
		freq = 0;
	}
	handle_ieee80211_packet(op,ehdr,len,freq);
	return;

malformed:
	op->malformed = 1;
	diagnostic("%s Packet too small (%zu) on %s",__func__,len,op->i->name);
}
