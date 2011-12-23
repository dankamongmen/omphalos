#include <iconv.h>
#include <errno.h>
#include <omphalos/tx.h>
#include <omphalos/diag.h>
#include <omphalos/lltd.h>
#include <omphalos/service.h>
#include <omphalos/netaddrs.h>
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
	LLTD_ENDOFPROP = 0x0,
	LLTD_HOSTID = 0x1,
	LLTD_CHARACTERISTICS = 0x2,
	LLTD_PHYMEDIUM = 0x3,
	LLTD_WIRELESSMODE = 0x4,
	LLTD_BSSID = 0x5,
	LLTD_SSID = 0x6,
	LLTD_IPV4 = 0x7,
	LLTD_IPV6 = 0x8,
	LLTD_MAXRATE = 0x9,
	LLTD_PERFCNTFREQ = 0xa,
	LLTD_LINKSPEED = 0xc,
	LLTD_RSSI = 0xd,
	LLTD_ICON = 0xe,
	LLTD_NAME = 0xf,
	LLTD_SUPPORTINFO = 0x10,
	LLTD_FRIENDLYNAME = 0x11,
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

struct lltd_characteristics {
	unsigned pubnat: 1;
	unsigned privnat: 1;
	unsigned fulldpx: 1;
	unsigned httpmgmt: 1;
	unsigned loopback: 1;
	unsigned reserved: 11;
} __attribute__ ((packed));

#define LLTD_SSIDLEN_MAX 32

static iconv_t ucs2le;
static pthread_mutex_t iconvlock = PTHREAD_MUTEX_INITIALIZER;

int init_lltd_service(void){
	if((ucs2le = iconv_open("UTF-8","UCS-2LE")) == NULL){
		diagnostic("%s can't convert UCS-2LE to UTF-8 (%s)",__func__,strerror(errno));
		return -1;
	}
	return 0;
}

int stop_lltd_service(void){
	if(iconv_close(ucs2le)){
		diagnostic("%s error closing iconv (%s)",__func__,strerror(errno));
		return -1;
	}
	return 0;
}

char *ucs2le_to_utf8(char *ucs2,size_t len){
	size_t out = len / 2 + 1;
	int badconvert = 0;
	char *r,*end;

	if(len % 2){
		diagnostic("%s bad UCS-2LE length %zu",__func__,len);
		return NULL;
	}
	if((r = malloc(out)) == NULL){
		return NULL;
	}
	r[out - 1] = '\0';
	end = r;
	pthread_mutex_lock(&iconvlock);
	if(iconv(ucs2le,&ucs2,&len,&end,&out) == (size_t)-1){
		badconvert = 1;
	}
	pthread_mutex_unlock(&iconvlock);
	if(len || badconvert){
		diagnostic("%s couldn't convert UCS-2LE",__func__);
		free(r);
		return NULL;
	}
	if(!out){
		diagnostic("%s UCS-2LE too long",__func__);
		free(r);
		return NULL;
	}
	return r;
}

static void
handle_lltd_tlvs(omphalos_packet *op,const void *frame,size_t len){
	const struct lltdtlv *tlv = frame;
	const void *ip = NULL,*ip6 = NULL;
	char *name = NULL;

	// Every LLTD frame must have an End-of-Properties TLV
	while(len >= sizeof(*tlv)){
		const void *dat;

		if(len < tlv->length + sizeof(*tlv)){
			diagnostic("%s bad LLTD TLV length (%u) on %s",__func__,tlv->length,op->i->name);
			return;
		}
		dat = (const char *)tlv + sizeof(*tlv);
		switch(tlv->type){
			case LLTD_ENDOFPROP:{
			break;}case LLTD_HOSTID:{
				if(tlv->length != op->i->addrlen){
					diagnostic("%s bad LLTD HostID (%u) on %s",__func__,tlv->length,op->i->name);
				}
				// FIXME
			break;}case LLTD_CHARACTERISTICS:{
				const struct lltd_characteristics *chars = dat;
				if(tlv->length != sizeof(*chars)){
					diagnostic("%s bad LLTD characteristics (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_PHYMEDIUM:{
				// FIXME parse up the ifType MIB
				if(tlv->length != 4){
					diagnostic("%s bad LLTD ifType (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_WIRELESSMODE:{
				if(tlv->length != 1){
					diagnostic("%s bad LLTD IEEE 802.11 (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_BSSID:{
				if(tlv->length != ETH_ALEN){
					diagnostic("%s bad LLTD BSSID (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_SSID:{
				if(tlv->length > LLTD_SSIDLEN_MAX){
					diagnostic("%s bad LLTD SSID (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_IPV4:{
				if(tlv->length != 4){
					diagnostic("%s bad LLTD IPv4 (%u) on %s",__func__,tlv->length,op->i->name);
				}
				ip = dat;
			break;}case LLTD_IPV6:{
				if(tlv->length != 16){
					diagnostic("%s bad LLTD IPv6 (%u) on %s",__func__,tlv->length,op->i->name);
				}
				ip6 = dat;
			break;}case LLTD_MAXRATE:{
				// Units of 0.5Mbps, in NBO
				if(tlv->length != 2){
					diagnostic("%s bad LLTD maxrate (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_PERFCNTFREQ:{
				if(tlv->length != 8){
					diagnostic("%s bad LLTD PerfCntRate (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_LINKSPEED:{
				if(tlv->length != 4){
					diagnostic("%s bad LLTD MaxLinkSpeed (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_RSSI:{
				if(tlv->length != 4){
					diagnostic("%s bad LLTD RSSI (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_ICON:{
				if(tlv->length){
					diagnostic("%s bad LLTD Icon (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_NAME:{
				if(tlv->length < 2 || tlv->length > 32){
					diagnostic("%s bad LLTD MachineName (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_SUPPORTINFO:{
				if(tlv->length < 1 || tlv->length > 64){
					diagnostic("%s bad LLTD SupportInfo (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_FRIENDLYNAME:{
				// Buffalo routers encode the FriendlyName
				// directly (UCS-2LE) in violation of the spec!
				if(tlv->length){
					name = ucs2le_to_utf8((char *)dat,tlv->length);
				}
			break;}case LLTD_UUID:{
				if(tlv->length != 16){
					diagnostic("%s bad LLTD UUID (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_HARDWAREID:{
				if(tlv->length){
					diagnostic("%s bad LLTD HwID (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_QOSCHARACTERISTICS:{
				if(tlv->length != 4){
					diagnostic("%s bad LLTD QoSCharacteristics (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_80211_PHYMEDIUM:{
				if(tlv->length != 1){
					diagnostic("%s bad LLTD 80211PhyMedium (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_AP_TABLE:{
				if(tlv->length){
					diagnostic("%s bad LLTD APTable (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_ICON_DETAIL:{
				if(tlv->length){
					diagnostic("%s bad LLTD DetailIcon (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_SEESLIST:{
				if(tlv->length != 2){
					diagnostic("%s bad LLTD SeesList (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_COMPONENTS:{
				if(tlv->length){
					diagnostic("%s bad LLTD ComponentTable (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_REPEATER_LINEAGE:{
				if(tlv->length % ETH_ALEN){
					diagnostic("%s bad LLTD RepeatLineage (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}case LLTD_REPEATER_AP_TABLE:{
				if(tlv->length){
					diagnostic("%s bad LLTD RepeatTable (%u) on %s",__func__,tlv->length,op->i->name);
				}
			break;}default:
				diagnostic("%s unknown TLV (0x%02x) on %s",__func__,tlv->type,op->i->name);
				break;
		}
		// FIXME process TLV's
		len -= sizeof(*tlv) + tlv->length;
		tlv = (const struct lltdtlv *)((const char *)tlv + sizeof(*tlv) + tlv->length);
	}
	if(name){
		struct l3host *l3;

		if(ip){
			l3 = lookup_local_l3host(NULL,op->i,op->l2s,AF_INET,ip);
			name_l3host_absolute(op->i,op->l2s,l3,name,NAMING_LEVEL_MDNS);
		}
		if(ip6){
			l3 = lookup_local_l3host(NULL,op->i,op->l2s,AF_INET6,ip6);
			name_l3host_absolute(op->i,op->l2s,l3,name,NAMING_LEVEL_MDNS);
		}
		free(name);
	}
}

struct lltddischdr {
	uint16_t gennum;
	uint16_t numstations;
} __attribute__ ((packed));

struct lltdhellohdr {
	uint16_t gennum;
	char curmapper[ETH_ALEN];
	char appmapper[ETH_ALEN];
} __attribute__ ((packed));

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
			const struct lltddischdr *disc;

			// FIXME check for source
			disc = dframe;
			dframe = (const char *)dframe + sizeof(*disc);
			dlen -= sizeof(*disc);
			if(dlen % ETH_ALEN){ // station list
				diagnostic("%s malformed LLTD Hello (%zu) on %s",__func__,dlen,op->i->name);
				return;
			}
			break;
		}case TOPDISC_HELLO:{ // FIXME a responder!
			const struct lltdhellohdr *hello;

			hello = dframe;
			dframe = (const char *)dframe + sizeof(*hello);
			dlen -= sizeof(*hello);
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
