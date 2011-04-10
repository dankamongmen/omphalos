#include <omphalos/sll.h>
#include <omphalos/interface.h>

// A cooked packet has had its link-layer header stripped. An l2-neutral
// struct sockaddr_sll is put in its place.
void handle_cooked_packet(interface *iface,const unsigned char *pkt,size_t len){
	struct sockaddr_sll *sll = pkt;


}
