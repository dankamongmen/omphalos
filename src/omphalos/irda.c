#include <sys/socket.h>
#include <linux/irda.h>
#include <omphalos/diag.h>
#include <omphalos/irda.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

// taken from linux/net/irda
struct irlap_info {
        __u8 caddr;   /* Connection address */
        __u8 control; /* Frame type */
        __u8 cmd;

        __u32 saddr;
        __u32 daddr;

	unsigned char discover;
	unsigned char slot;
	unsigned char version;
} __attribute__ ((packed));

void handle_irda_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct irlap_info *hdr = frame;
	uint32_t addr;

	if(len < sizeof(*hdr)){
		op->malformed = 1;
		diagnostic("%s malformed with %zu",__func__,len);
		return;
	}
	memcpy(&addr,&hdr->saddr,sizeof(addr));
	op->l2s = lookup_l2host(op->i,&addr);
	memcpy(&addr,&hdr->daddr,sizeof(addr));
	op->l2d = lookup_l2host(op->i,&addr);
}
