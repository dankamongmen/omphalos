#include <sys/socket.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/eapol.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

struct eapolhdr {
	uint8_t version;
	uint8_t type;
	uint16_t len;
};

#define EAPOL_DATA	0
#define EAPOL_START	1
#define EAPOL_LOGOFF	2
#define EAPOL_KEY	3
#define EAPOL_ALERT	4

void handle_eapol_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct eapolhdr *eaphdr = frame;

	if(len < sizeof(*eaphdr)){
		diagnostic(L"%s truncated (%zu < %zu)",__func__,len,sizeof(*eaphdr));
		op->malformed = 1;
		return;
	}
	if(eaphdr->version != 1 && eaphdr->version != 2){
		diagnostic(L"Unknown EAPOL version %u",eaphdr->version);
		op->noproto = 1;
	}
	if(ntohs(eaphdr->len) != len - sizeof(*eaphdr)){
		diagnostic(L"%s malformed (%u != %zu)",__func__,
			ntohs(eaphdr->len),len - sizeof(*eaphdr));
		op->malformed = 1;
		return;
	}
	switch(eaphdr->type){
	case EAPOL_DATA:{
	break;}case EAPOL_START:{
	break;}case EAPOL_LOGOFF:{
	break;}case EAPOL_KEY:{
	break;}case EAPOL_ALERT:{
	break;}default:{
		diagnostic(L"%s noproto %u",__func__,eaphdr->type);
		op->noproto = 1;
	break;} }
}
