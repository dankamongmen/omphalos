#include <omphalos/tx.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Acquire a frame from the ringbuffer
void *get_tx_frame(const omphalos_iface *octx,interface *i){
	if(i->fd < 0){
		octx->diagnostic("%s is not prepared for transmission",i->name);
		return NULL;
	}
	return NULL; // FIXME
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
