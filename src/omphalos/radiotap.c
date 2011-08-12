#include <sys/socket.h>
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

typedef struct ieee80211beacon {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_src[ETH_ALEN];
	unsigned char bssid[ETH_ALEN];
	uint16_t fraqseq;
} __attribute__ ((packed)) ieee80211beacon;

#define MANAGEMENT_FRAME		0
#define IEEE80211_SUBTYPE_BEACON	8
#define CONTROL_FRAME			1
#define DATA_FRAME			2

#define IEEE80211_VERSION(ctrl) ((ntohs(ctrl) & 0x0300) >> 8u)
#define IEEE80211_TYPE(ctrl) ((ntohs(ctrl) & 0x0c00) >> 10u)
#define IEEE80211_SUBTYPE(ctrl) ((ntohs(ctrl) & 0xf000) >> 12u)

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
	// FIXME handle more, extract ESSID etc
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

void handle_radiotap_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const radiotaphdr *rhdr = frame;
	const void *ehdr;
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
	ehdr = (const char *)frame + rlen;
	len -= rlen;
	handle_ieee80211_packet(octx,op,ehdr,len);
}
