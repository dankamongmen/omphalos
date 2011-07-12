#include <stdio.h>
#include <assert.h>
#include <omphalos/tx.h>
#include <omphalos/psocket.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Acquire a frame from the ringbuffer
void *get_tx_frame(const omphalos_iface *octx,interface *i,size_t *fsize){
	struct tpacket_hdr *thdr = i->curtxm;

	// FIXME need also check for TP_WRONG_FORMAT methinks?
	if(thdr->tp_status != TP_STATUS_AVAILABLE){
		octx->diagnostic("No available TX frames on %s",i->name);
		return NULL;
	}
	i->curtxm += inclen(&i->txidx,&i->ttpr);
	*fsize = i->ttpr.tp_frame_size;
	return NULL;
}

// Mark a frame as ready-to-send. Must have come from get_tx_frame() using this
// same interface.
void send_tx_frame(const omphalos_iface *octx,interface *i,void *frame){
	struct tpacket_hdr *thdr = frame;

	++i->txframes;
	thdr->tp_status = TP_STATUS_SEND_REQUEST;
	if(send(i->fd,NULL,0,0) < 0){
		octx->diagnostic("Error transmitting on %s",i->name);
	}
}

void prepare_arp_req(const omphalos_iface *octx,const interface *i,
		void *frame,size_t *flen,const void *paddr,size_t pln){
	assert(octx && i && frame && flen && paddr && pln);
}
