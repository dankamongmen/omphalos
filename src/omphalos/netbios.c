#include <string.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <netinet/ip6.h>
#include <omphalos/tx.h>
#include <omphalos/ip.h>
#include <omphalos/udp.h>
#include <omphalos/csum.h>
#include <omphalos/diag.h>
#include <omphalos/netbios.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// Returns 1 for a valid NetBIOS response, -1 for a valid NetBIOS query, 0 otherwise
int handle_netbios_packet(omphalos_packet *op,const void *frame,size_t len){
	assert(op && frame && len);
	return 0;
}
