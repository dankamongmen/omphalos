#include <omphalos/tx.h>
#include <omphalos/diag.h>
#include <omphalos/lltd.h>
#include <omphalos/service.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// FIXME it's actually the range 0x000D3AD7F140 through 0x000D3AFFFFFF
#define LLTD_L2_ADDR "\x00\x0D\x3A\xF1\xF1\xF1"

typedef struct lltdhdr {
	uint8_t version;
	uint8_t tos;
	uint8_t reserved;
	uint8_t function;
} __attribute__ ((packed)) lltdhdr;

#define TOS_TOP_DISCOVERY	0x00
#define TOS_QUICK_DISCOVERY	0x01
#define TOS_QOS_DIAGNOSTICS	0x02

#define LLTD_VERSION 1

enum {
	TOPDISC_DISCOVER = 0,
	TOPDISC_HELLO = 1,
	TOPDISC_EMIT = 2,
	TOPDISC_TRAIN = 3,
	TOPDISC_PROBE = 4,
	TOPDISC_ACK = 5,
	TOPDISC_QUERY = 6,
	TOPDISC_QUERYRESP = 7,
	TOPDISC_RESET = 8,
	TOPDISC_CHARGE = 9,
	TOPDISC_FLAT = 0xa,
	TOPDISC_QUERYLARGE = 0xb,
	TOPDISC_QUERYLARGERESP = 0xc,
};

// Used with TOS_TOP_DISCOVERY and TOS_QUICK_DISCOVERY
struct lltdbasehdr {
	unsigned char dst[ETH_ALEN];
	unsigned char src[ETH_ALEN];
	uint16_t seq;
} __attribute__ ((packed)) lltdbasehdr;

struct lltdtlv {
	uint8_t type;
	uint8_t length;
} __attribute__ ((packed)) lltdtlv;

enum {
	LLTD_ENDOFPROP = 0,
	LLTD_HOSTID = 1,
	LLTD_CHARACTERISTICS = 2,
	LLTD_PHYMEDIUM = 3,
	LLTD_WIRELESSMODE = 4,
	LLTD_BSSID = 5,
	LLTD_SSID = 6,
	LLTD_IPV4 = 7,
	LLTD_IPV6 = 8,
	LLTD_MAXRATE = 9,
	LLTD_PERFCNTFREQ = 10,
	LLTD_LINKSPEED = 12,
	LLTD_RSSI = 13,
	LLTD_ICON = 14,
	LLTD_NAME = 15,
	LLTD_SUPPORTINFO = 16,
	LLTD_FRIENDLYNAME = 17,
	LLTD_UUID = 0x12,
	LLTD_HARDWAREID = 0x13,
	LLTD_QOSCHARACTERISTICS = 0x14,
	LLTD_80211_PHYMEDIUM = 0x15,
	LLTD_AP_TABLE = 0x16,
	LLTD_ICON_DETAIL = 0x18,
	LLTD_SEESLIST = 0x19,
	LLTD_COMPONENTS = 0x1a,
	LLTD_REPEATER_LINEAGE = 0x1b,
	LLTD_REPEATER_AP_TABLE = 0x1c,
};

static void
handle_lltd_tlvs(omphalos_packet *op,const void *frame,size_t len){
	const struct lltdtlv *tlv = frame;
	int eop = 0;

	// Every LLTD frame must have an End-of-Properties TLV
	while(len >= sizeof(*tlv)){
		if(len < tlv->length + sizeof(*tlv)){
			diagnostic("%s bad LLTD TLV length (%u) on %s",__func__,tlv->length,op->i->name);
			return;
		}
		switch(tlv->type){
			case LLTD_ENDOFPROP:
				eop = 1;
				break;
			case LLTD_HOSTID:
				if(tlv->length != op->i->addrlen){
					diagnostic("%s bad LLTD HostID (%u) on %s",__func__,tlv->length,op->i->name);
				}
				// FIXME
				break;
			default:
				diagnostic("%s unknown TLV (%u) on %s",__func__,tlv->type,op->i->name);
				break;
		}
		// FIXME process TLV's
		len -= sizeof(*tlv) + tlv->length;
		tlv = (const struct lltdtlv *)((const char *)tlv + sizeof(*tlv) + tlv->length);
	}
	if(!eop){
		diagnostic("%s LLTD lacked EoP on %s",__func__,op->i->name);
	}
}

static void
handle_lltd_discovery(omphalos_packet *op,unsigned function,const void *frame,
							size_t flen){
	const struct lltdbasehdr *base = frame;
	const void *dframe;
	size_t dlen;

	if(flen < sizeof(*base)){
		diagnostic("%s malformed with %zu on %s",__func__,flen,op->i->name);
		op->malformed = 1;
		return ;
	}
	dframe = ((const char *)base + sizeof(*base));
	dlen = flen - sizeof(*base);
	switch(function){
		case TOPDISC_DISCOVER:{
			// FIXME check for source
			handle_lltd_tlvs(op,dframe,dlen);					      
			break;
		}case TOPDISC_HELLO:{
			// FIXME a responder!
			handle_lltd_tlvs(op,dframe,dlen);					      
			break;
		}case TOPDISC_EMIT:
		case TOPDISC_TRAIN:
		case TOPDISC_PROBE:
		case TOPDISC_ACK:
		case TOPDISC_QUERY:
		case TOPDISC_QUERYRESP:
		case TOPDISC_RESET:
		case TOPDISC_CHARGE:
		case TOPDISC_FLAT:
		case TOPDISC_QUERYLARGE:
		case TOPDISC_QUERYLARGERESP:{
			break;
		}default:{
			diagnostic("%s unknown function %u on %s",__func__,function,op->i->name);
			op->noproto = 1;
			return;
		}
	}
}

void handle_lltd_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct lltdhdr *lltd = frame;
	const void *dgram;
	size_t dlen;

	if(len < sizeof(*lltd)){
		diagnostic("%s malformed with %zu on %s",__func__,len,op->i->name);
		op->malformed = 1;
		return;
	}
	if(lltd->version != LLTD_VERSION){
		diagnostic("%s unknown LLTD version %u on %s",__func__,lltd->version,op->i->name);
		op->noproto = 1;
		return;
	}
	dlen = len - sizeof(*lltd);
	dgram = (const char *)frame + sizeof(*lltd);
	switch(lltd->tos){
		case TOS_TOP_DISCOVERY: case TOS_QUICK_DISCOVERY:{
			handle_lltd_discovery(op,lltd->function,dgram,dlen);
			break;
		}case TOS_QOS_DIAGNOSTICS:{
			break;
		}default:{
			diagnostic("%s unknown ToS (%u) on %s",__func__,lltd->tos,op->i->name);
			op->noproto = 1;
			return;
		}
	}
}

int initiate_lltd(int fam,interface *i,const void *addr){
	struct tpacket_hdr *thdr;
	struct lltdbasehdr *base;
	size_t flen,tlen;
	lltdhdr *lltd;
	void *frame;
	int r;

	if(i->addrlen != ETH_ALEN || !i->bcast){
		return -1;
	}
	if((frame = get_tx_frame(i,&flen)) == NULL){
		return -1;
	}
	thdr = frame;
	tlen = thdr->tp_mac;
	if((r = prep_eth_bcast((char *)frame + tlen,flen - tlen,i,ETH_P_LLTD)) < 0){
		goto err;
	}
	tlen += r;
	if(flen - tlen < sizeof(*lltd)){
		goto err;
	}
	lltd = (lltdhdr *)((const char *)frame + tlen);
	if(fam == AF_INET){
		assert(addr); // FIXME
	}else if(fam == AF_INET6){
		assert(addr); // FIXME
	}
	lltd->version = LLTD_VERSION;
	lltd->tos = TOS_QUICK_DISCOVERY;
	lltd->reserved = 0;
	lltd->function = TOPDISC_DISCOVER;
	tlen += sizeof(*lltd);
	if(flen - tlen < sizeof(*base)){
		goto err;
	}
	base = (struct lltdbasehdr *)((const char *)frame + tlen);
	memcpy(base->dst,i->bcast,ETH_ALEN);
	memcpy(base->src,i->addr,ETH_ALEN);
	base->seq = 0; // always 0 for discovery
	tlen += sizeof(*base);
	thdr->tp_len = tlen - sizeof(*thdr);
	return send_tx_frame(i,frame);

err:
	abort_tx_frame(i,frame);
	return -1;
}
