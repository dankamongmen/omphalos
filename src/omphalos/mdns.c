#include <assert.h>
#include <sys/socket.h>
#include <omphalos/tx.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <omphalos/route.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

void handle_mdns_packet(const omphalos_iface *iface,omphalos_packet *op,
			const void *frame,size_t len){
	handle_dns_packet(iface,op,frame,len);
}

int tx_mdns_ptr(const omphalos_iface *octx,interface *i,int fam,const char *str){
	void *mcast_addr = NULL;
	struct routepath rp;
	size_t flen;
	void *frame;

	assert(fam == AF_INET || fam == AF_INET6);
	if(!(i->flags & IFF_MULTICAST)){
		return 0;
	}
	frame = get_tx_frame(octx,i,&flen);
	if(setup_dns_ptr(&rp,fam,flen,mcast_addr,frame,str)){
		abort_tx_frame(octx,i,frame);
		return -1;
	}
	send_tx_frame(octx,i,frame);
	return 0;
}
