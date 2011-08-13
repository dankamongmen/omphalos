#include <limits.h>
#include <assert.h>
#include <sys/socket.h>
#include <omphalos/util.h>
#include <omphalos/hwaddrs.h>
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

typedef struct ieee80211hdr {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
	// FIXME unsigned char h_src[ETH_ALEN]; see below
} __attribute__ ((packed)) ieee80211hdr;

// IEEE 802.11 beacon header. Followed by an IEEE 802.11 management frame.
typedef struct ieee80211beacon {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_src[ETH_ALEN];
	unsigned char bssid[ETH_ALEN];
	uint16_t fraqseq;
} __attribute__ ((packed)) ieee80211beacon;

// Fixed portion (12 bytes) of an IEEE 802.11 management frame. Followed by a
// tagged variable-length portion.
typedef struct ieee80211mgmt {
	uint64_t timestamp;
	uint16_t interval;
	uint16_t capabilities;
} __attribute__ ((packed)) ieee80211mgmt;

#define MANAGEMENT_FRAME		0
#define IEEE80211_SUBTYPE_BEACON	8
#define CONTROL_FRAME			1
#define DATA_FRAME			2

#define IEEE80211_VERSION(ctrl) ((ntohs(ctrl) & 0x0300) >> 8u)
#define IEEE80211_TYPE(ctrl) ((ntohs(ctrl) & 0x0c00) >> 10u)
#define IEEE80211_SUBTYPE(ctrl) ((ntohs(ctrl) & 0xf000) >> 12u)

#define IEEE80211_MGMT_TAG_SSID		0
#define IEEE80211_MGMT_TAG_RATES	1

static void
handle_ieee80211_mgmt(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const ieee80211mgmt *imgmt = frame;
	struct {
		void *ptr;
		size_t len;
	} tagtbl[1u << CHAR_BIT] = {};
	const unsigned char *tags;
	unsigned z;

	if(len < sizeof(*imgmt)){
		++op->i->malformed;
		octx->diagnostic("%s mgmt frame too small (%zu) on %s",
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
			octx->diagnostic("%s bad mgmt taglen (%zu/%u) on %s",
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
			octx->diagnostic("%s bad mgmt tags (%zu) on %s",
					__func__,len,op->i->name);
		}
		++op->i->malformed;
		goto freetags;
	}
	// FIXME do stuff

freetags:
	for(z = 0 ; z < sizeof(tagtbl) / sizeof(*tagtbl) ; ++z){
		free(tagtbl[z].ptr);
	}
}

static void
handle_ieee80211_beacon(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const ieee80211beacon *ibec = frame;

	if(len < sizeof(*ibec)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	op->l2s = lookup_l2host(octx,op->i,ibec->h_src);
       	len -= sizeof(*ibec);
	handle_ieee80211_mgmt(octx,op,(const char *)frame + sizeof(*ibec),len);
}

static void
handle_ieee80211_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const ieee80211hdr *ihdr = frame;

	// FIXME certain packets don't have the full 802.11 header (8 bytes,
	// control/duration/h_dest, seems to be the minimum).
	if(len < sizeof(ieee80211hdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	if(IEEE80211_VERSION(ihdr->control) != 0){
		++op->i->noprotocol;
		octx->diagnostic("%s Unknown version (%zu) on %s",__func__,
				IEEE80211_VERSION(ihdr->control),op->i->name);
		return;
	}
	op->l2s = NULL;
	op->l2d = lookup_l2host(octx,op->i,ihdr->h_dest);
	switch(IEEE80211_TYPE(ihdr->control)){
		case MANAGEMENT_FRAME:{
			if(IEEE80211_SUBTYPE(ihdr->control) == IEEE80211_SUBTYPE_BEACON){
				handle_ieee80211_beacon(octx,op,frame,len);
			}
		}break;
		case CONTROL_FRAME:{
		}break;
		case DATA_FRAME:{
		}break;
		default:{
			++op->i->noprotocol;
			octx->diagnostic("%s Unknown type %zu on %s",__func__,
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

void handle_radiotap_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const radiotaphdr *rhdr = frame;
	const void *ehdr,*vhdr;
	uint32_t pres;
	unsigned rlen;

	// FIXME certain packets don't have the full 802.11 header (8 bytes,
	// control/duration/h_dest, seems to be the minimum).
	if(len < sizeof(radiotaphdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	if(rhdr->version != 0){
		++op->i->noprotocol;
		octx->diagnostic("%s Unknown radiotap version %zu on %s",
				__func__,rhdr->version,op->i->name);
		return;
	}
	rlen = rhdr->len;
	if(len < rlen){
		++op->i->malformed;
		octx->diagnostic("%s Radiotap too small (%zu < %zu) on %s",
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
	}
	if(pres & (1u << IEEE80211_RADIOTAP_FLAGS)){
		unsigned flags;
		if(rlen < 1){
			goto malformed;
		}
		flags = *(const unsigned char *)vhdr;
		++vhdr;
		// There's a 32-bit FCS on the end built into the length here,
		// if the FCS bit is set in Flags. This affects actual 'len'.
		if(flags & IEEE80211_RADIOTAP_F_FCS){
			if(len < 4){
				goto malformed;
			}
			len -= 4;
		}
	}
	handle_ieee80211_packet(octx,op,ehdr,len);
	return;

malformed:
	++op->i->malformed;
	octx->diagnostic("%s Packet too small (%zu) on %s",
			__func__,len,op->i->name);
}
