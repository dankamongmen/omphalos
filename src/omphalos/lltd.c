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

void handle_lltd_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct lltdhdr *lltd = frame;

	if(len < sizeof(*lltd)){
		diagnostic("%s malformed with %zu",__func__,len);
		op->malformed = 1;
		return;
	}
	if(lltd->version != LLTD_VERSION){
		diagnostic("%s unknown LLTD version %u",__func__,lltd->version);
		op->noproto = 1;
		return;
	}
	switch(lltd->tos){
		case TOS_TOP_DISCOVERY: case TOS_QUICK_DISCOVERY:{
			if(lltd->function > TOPDISC_QUERYLARGERESP){
				diagnostic("%s unknown function %u",__func__,lltd->function);
				return;
			}
		break;}
	}
}

int initiate_lltd(int fam,interface *i,const void *addr){
	struct tpacket_hdr *thdr;
	size_t flen,tlen;
	//lltdhdr *lltd;
	void *frame;
	int r;

	if((frame = get_tx_frame(i,&flen)) == NULL){
		return -1;
	}
	thdr = frame;
	tlen = thdr->tp_mac;
	if((r = prep_eth_header((char *)frame + tlen,flen - tlen,i,LLTD_L2_ADDR,ETH_P_LLTD)) < 0){
		goto err;
	}
	tlen += r;
	//lltd = (lltdhdr *)((const char *)frame + tlen);
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
	thdr->tp_len = tlen - sizeof(*thdr);
	return send_tx_frame(i,frame);

err:
	abort_tx_frame(i,frame);
	return -1;
}
