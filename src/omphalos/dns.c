#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <omphalos/dns.h>
#include <asm/byteorder.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define DNS_CLASS_IN	__constant_ntohs(1u)
#define DNS_TYPE_A	__constant_ntohs(1u)
#define DNS_TYPE_PTR	__constant_ntohs(12u)
#define DNS_TYPE_AAAA	__constant_ntohs(28u)

struct dnshdr {
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount,ancount,nscount,arcount;
	// question, answer, authority, and additional sections follow
};

// FIXME is it safe to be using (possibly signed) naked chars?
// FIXME handle AAAA lookups (".ip6.arpa")!
static int
process_reverse_lookup(const char *buf,int *fam,struct sockaddr_storage *addr){
	const size_t len = strlen(buf);
	char obuf[INET6_ADDRSTRLEN];
	const char *chunk,*c;
	unsigned quad;
	char q[4][5];

	if(len < __builtin_strlen(".in-addr.arpa")){
		return -1;
	}
	const size_t xlen = len - __builtin_strlen(".in-addr.arpa");
	if(strcmp(buf + xlen,".in-addr.arpa")){
		return -1;
	}
	// Don't need to worry about checks against len, since we'll hit 'i'
	// from 'in-addr.arpa' and exit.
	chunk = buf;
	for(quad = 0 ; quad < 4 ; ++quad){
		for(c = chunk ; c - chunk < 3 ; ++c){
			if(isdigit(*c)){
				q[quad][c - chunk] = *c;
			}else if(*c == '.'){
				break;
			}else{
				return -1;
			}
		}
		if(c - buf == 3){
			return -1;
		}
		q[quad][c - chunk] = '.';
		q[quad][c - chunk + 1] = '\0';
		chunk = c + 1;
	}
	*obuf = '\0';
	while(quad--){
		strcat(obuf,q[quad]);
	}
	obuf[xlen] = '\0';
	if(inet_pton(AF_INET,obuf,addr) != 1){
		return -1;
	}
	*fam = AF_INET;
	return 0;
}

static char *
extract_dns_record(size_t len,const unsigned char *sec,unsigned *class,
			unsigned *type,unsigned *bsize){
	unsigned char rlen;
	char *buf = NULL;
	unsigned idx = 0;

	*bsize = 0;
	while(len > ++idx && (rlen = *sec++)){
		char *tmp;

		if(rlen >= 192 || rlen + idx > len){
			free(buf);
			return NULL;
		}
		// If there was any previous length, it was
		// nul-terminated. We will be writing over said nul with
		// a '.', so count all that length. We'll then need the
		// new characters (rlen), and a nul term.
		if((tmp = realloc(buf,*bsize + rlen + 1)) == NULL){
			free(buf);
			return NULL;
		}
		buf = tmp;
		if(*bsize){
			buf[*bsize - 1] = '.';
		}
		strncpy(buf + *bsize,(const char *)sec,rlen);
		*bsize += rlen + 1;
		buf[*bsize - 1] = '\0';
		sec += rlen;
		idx += rlen;
	}
	if(len < idx + 4 || buf == NULL){
		free(buf);
		return NULL;
	}
	*class = *((uint16_t *)sec + 1);
	*type = *(uint16_t *)sec;
	*bsize += 4;
	return buf;
}

void handle_dns_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct dnshdr *dns = frame;
	const unsigned char *sec;
	uint16_t qd,an,ns,ar;

	if(len < sizeof(*dns)){
		octx->diagnostic("%s malformed with %zu on %s",
				__func__,len,op->i->name);
		++op->i->malformed;
		return;
	}
	//opcode = (ntohs(dns->flags) & 0x7800) >> 11u;
	qd = ntohs(dns->qdcount);
	assert(qd == 1);
	an = ntohs(dns->ancount);
	ns = ntohs(dns->nscount);
	ar = ntohs(dns->arcount);
	len -= sizeof(*dns);
	sec = (const unsigned char *)frame + sizeof(*dns);
	//octx->diagnostic("q/a/n/a: %hu/%hu/%hu/%hu",qd,an,ns,ar);
	if(qd){
		unsigned class,type,bsize;
		char *buf;

		buf = extract_dns_record(len,sec,&class,&type,&bsize);
		if(buf == NULL){
			goto malformed;
		}
		if(class == DNS_CLASS_IN){
			if(type == DNS_TYPE_PTR){
				struct sockaddr_storage ss;
				int fam;

				if(process_reverse_lookup(buf,&fam,&ss)){
					goto malformed;
				}
			}
			//octx->diagnostic("TYPE: %hu CLASS: %hu",
			//		,ntohs(*((uint16_t *)sec + 1)));
		}
		// FIXME handle A/AAAA
		free(buf);
		sec += bsize;
		len -= bsize;
	}
	if(an){
		if(len == 0){
			goto malformed;
		}
	}
	if(ns){
		if(len == 0){
			goto malformed;
		}
	}
	if(ar){
		if(len == 0){
			goto malformed;
		}
	}
	return;

malformed:
	octx->diagnostic("%s malformed with %zu on %s",
			__func__,len,op->i->name);
	++op->i->malformed;
	return;
}
