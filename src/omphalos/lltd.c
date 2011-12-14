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

static void
handle_lltd_discovery(omphalos_packet *op,unsigned function,const void *frame,
							size_t flen){
	const struct lltdbasehdr *base;

	if(flen < sizeof(*base)){
		diagnostic("%s malformed with %zu on %s",__func__,flen,op->i->name);
		op->malformed = 1;
		return ;
	}
	base = (const struct lltdbasehdr *)frame;
	switch(function){
		case TOPDISC_DISCOVER:{
			// FIXME check for source
			break;
		}case TOPDISC_HELLO:{
			// FIXME a responder!
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
