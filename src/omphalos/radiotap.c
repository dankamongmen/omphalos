#include <sys/socket.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethernet.h>
#include <omphalos/radiotap.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME i guess we need a copy of the upstream, since linux doesn't
// seem to install it? dubious, very dubious...
typedef struct radiotaphdr {
	char data[26];
} __attribute__ ((packed)) radiotaphdr;

typedef struct ieee80211hdr {
	uint16_t control;
	uint16_t duration;
	unsigned char hwaddr[ETH_ALEN];
} __attribute__ ((packed)) ieee80211hdr;

static void
handle_ieee80211_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const ieee80211hdr *ihdr = frame;
	struct l2host *l2s;

	if(len < sizeof(ieee80211hdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	l2s = lookup_l2host(octx,op->i,ihdr->hwaddr);
	op->l2s = l2s;
}

void handle_radiotap_packet(const omphalos_iface *octx,omphalos_packet *op,
				const void *frame,size_t len){
	const char *sframe;

	if(len < sizeof(radiotaphdr)){
		++op->i->malformed;
		octx->diagnostic("%s Packet too small (%zu) on %s",
				__func__,len,op->i->name);
		return;
	}
	sframe = (const char *)frame + sizeof(radiotaphdr);
	len -= sizeof(radiotaphdr);
	handle_ieee80211_packet(octx,op,sframe,len);
}
