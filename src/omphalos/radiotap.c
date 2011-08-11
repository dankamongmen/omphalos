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

typedef struct ieee80211hdr {
	uint16_t control;
	uint16_t duration;
	unsigned char h_dest[ETH_ALEN];
	// FIXME unsigned char h_src[ETH_ALEN]; see below
} __attribute__ ((packed)) ieee80211hdr;

static void
handle_ieee80211_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const ieee80211hdr *ihdr = frame;
	struct l2host *l2s;

	// FIXME certain packets don't have the full 802.11 header (8 bytes,
	// control/duration/h_dest, seems to be the minimum).
	if(len < sizeof(ieee80211hdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	l2s = lookup_l2host(octx,op->i,ihdr->h_dest);
	op->l2s = l2s;
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
