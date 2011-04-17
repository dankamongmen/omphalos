#include <omphalos/eapol.h>
#include <omphalos/interface.h>

struct eapolhdr {
	uint8_t version;
	uint8_t type;
	uint16_t len;
} eapolhdr;

#define EAPOL_DATA	0
#define EAPOL_START	1
#define EAPOL_LOGOFF	2
#define EAPOL_KEY	3
#define EAPOL_ALERT	4

void handle_eapol_packet(interface *i,const void *frame,size_t len){
	const struct eapolhdr *eaphdr = frame;

	if(len < sizeof(*eaphdr)){
		fprintf(stderr,"%s truncated (%zu < %zu)\n",__func__,len,sizeof(*eaphdr));
		++i->malformed;
		return;
	}
	if(eaphdr->version != 1){
		fprintf(stderr,"Unknown EAPOL version %u\n",eaphdr->version);
		++i->noprotocol;
	}
	if(eaphdr->len != len - sizeof(*eaphdr)){
		fprintf(stderr,"%s malformed (%u != %zu)\n",__func__,eaphdr->len,len - sizeof(*eaphdr));
		++i->malformed;
		return;
	}
	switch(eaphdr->type){
	case EAPOL_DATA:{
	break;}case EAPOL_START:{
	break;}case EAPOL_LOGOFF:{
	break;}case EAPOL_KEY:{
	break;}case EAPOL_ALERT:{
	break;}default:{
		fprintf(stderr,"%s noproto %u\n",__func__,eaphdr->type);
		++i->noprotocol;
	break;} }
}
