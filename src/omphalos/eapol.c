#include <omphalos/eapol.h>
#include <omphalos/interface.h>

void handle_eapol_packet(interface *i,const void *frame,size_t len){
	printf("PAE: %s %p %zu\n",i->name,frame,len);
}
