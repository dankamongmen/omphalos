#include <arpa/inet.h>
#include <omphalos/ip.h>
#include <omphalos/sll.h>
#include <linux/if_arp.h>
#include <asm/byteorder.h>
#include <linux/if_packet.h>
#include <omphalos/interface.h>

// A cooked packet has had its link-layer header stripped. An l2-neutral
// struct sockaddr_sll is put in its place.
void handle_cooked_packet(interface *iface,const unsigned char *pkt,size_t len){
}
