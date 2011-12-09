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

/* flags field
struct {
	unsigned response: 1;
	unsigned opcode: 4;
	unsigned unused1: 1;
	unsigned trunc: 1;
	unsigned recurse: 1;
	unsigned unused2: 3;
	unsigned bcast: 1;
	unsigned unused3: 4;
} __attribute__ ((packed));*/

typedef struct smbnshdr {
	uint16_t tid;
	uint16_t flags;
	uint16_t qc,ac,addc,authc;
} __attribute__ ((packed)) smbnshdr;

#define OPCODE_MASK 0x7800

#define OPCODE_REGISTRATION 0x2800 // (5)

char *first_level_decode(const char *enc,size_t len){
	size_t blen = 0,off = 0,slen;
	unsigned odd = 0;
	char *ret = NULL;

	if(len == 0){
		diagnostic("%s truncated First-Level Encoding",__func__);
		return NULL;
	}
	slen = *enc;
	if(slen + 2 > len){
		diagnostic("%s short First-Level Encoding (want %zu)",__func__,slen);
		return NULL;
	}
	--len;
	++enc;
	while(len && *enc >= 'A' && *enc <= 'P'){
		if((off + 1) >= blen){
			char *tmp;

			if((tmp = realloc(ret,blen + 16)) == NULL){
				free(ret);
				return NULL;
			}
			ret = tmp;
			blen += 16;
		}
		--len;
		++enc;
		// FIXME fill it in with decode
	}
	if(len == 0){
		diagnostic("%s truncated First-Level Encoding",__func__);
		return NULL;
	}
	if(odd){
		diagnostic("%s invalid First-Level Encoding",__func__);
		return NULL;
	}
	if(ret == NULL){
		diagnostic("%s empty First-Level Encoding",__func__);
		return NULL;
	}
	ret[off] = '\0';
	return ret;
}

int handle_netbios_ns_packet(omphalos_packet *op,const void *frame,size_t len){
	const smbnshdr *ns = frame;
	uint16_t f;

	if(len < sizeof(*ns)){
		diagnostic("%s NetBIOS NS too small (%zu) on %s",__func__,len,op->i->name);
		op->malformed = 1;
		return -1;
	}
	f = ntohs(ns->flags);
	switch(f & OPCODE_MASK){
		case OPCODE_REGISTRATION:{
			char *name;

			if((name = first_level_decode((const char *)ns + sizeof(*ns),
							len - sizeof(*ns))) == NULL){
				return -1;
			}
			diagnostic("NetBIOS name: [%s]",name); // FIXME
			break;
		}
	}
	return 0;
}
