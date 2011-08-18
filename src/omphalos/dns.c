#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <omphalos/dns.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/resolv.h>
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
process_reverse_lookup(const char *buf,int *fam,void *addr){
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
		for(c = chunk ; c - chunk < 4 ; ++c){
			if(isdigit(*c)){
				q[quad][c - chunk] = *c;
			}else if(*c == '.'){
				q[quad][c - chunk] = '.';
				break;
			}else{
				return -1;
			}
		}
		if(c - chunk == 4){
			return -1;
		}
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

static inline size_t
ustrlen(const unsigned char *s){
	return strlen((const char *)s);
}

static inline char *
ustrcat(char *to,const unsigned char *s){
	return strcat(to,(const char *)s);
}

static inline char *
ustrcpy(char *to,const unsigned char *s){
	return strcpy(to,(const char *)s);
}

// inflate the previous dns data at offset into buf, which we are responsible
// for resizing. data must have been previous, or else we can go into a loop!
static char *
dns_inflate(char *buf,unsigned *bsize,const unsigned char *orig,unsigned offset,
				unsigned curoffset){
	size_t z,zz;

	if(curoffset <= offset){ // forward references are disallowed
		return NULL;
	}
	z = ustrlen(orig + offset);
	if((buf = realloc(buf,*bsize + z + 1)) == NULL){
		return NULL; // caller is responsible for free()ing
	}
	if(*bsize){
		buf[*bsize - 1] = '.';
	}
	zz = 0;
	while(orig[offset + zz]){
		if(orig[offset + zz] & 0xc0){
			if((orig[offset + zz] & 0xc0) == 0xc0){
				unsigned newoffset;
				char *tmp;

				newoffset = ((orig[offset + zz] & ~0xc0) << 8u) + orig[offset + zz + 1];
				if((tmp = dns_inflate(buf,bsize,orig,newoffset,offset + zz)) == NULL){
					free(buf);
				}
				return tmp;
			}
			return NULL;
		}
		if(zz + orig[offset + zz] >= z){
			return NULL;
		}
		memcpy(buf + *bsize,orig + offset + zz + 1,orig[offset + zz]);
		*bsize += orig[offset + zz] + 1;
		zz += orig[offset + zz] + 1;
		buf[*bsize - 1] = '.';
	}
	buf[*bsize - 1] = '\0';
	return buf;
}

static char *
extract_dns_record(size_t len,const unsigned char *sec,unsigned *class,
			unsigned *type,unsigned *idx,const unsigned char *orig){
	unsigned char rlen;
	unsigned bsize = 0;
	char *buf = NULL;

	*idx = 0;
	while(len > ++*idx && (rlen = *sec++)){
		char *tmp;

		if((rlen & 0xc0) == 0xc0){
			unsigned offset;

			if(*idx > len){
				free(buf);
				return NULL;
			}
			offset = ((rlen & ~0xc0) << 8u) + *sec;
			if(offset >= sec - orig){
				free(buf);
				return NULL;
			}
			if((tmp = dns_inflate(buf,&bsize,orig,offset,sec - orig)) == NULL){
				free(buf);
				return NULL;
			}
			buf = tmp;
			++sec;
			++*idx;
			break;
		}else if((rlen & 0xc0) != 0x0){	// not allowed
			free(buf);
			return NULL;
		}else if(rlen + *idx > len){
			free(buf);
			return NULL;
		}
		// If there was any previous length, it was
		// nul-terminated. We will be writing over said nul with
		// a '.', so count all that length. We'll then need the
		// new characters (rlen), and a nul term.
		if((tmp = realloc(buf,bsize + rlen + 1)) == NULL){
			free(buf);
			return NULL;
		}
		buf = tmp;
		if(bsize){
			buf[bsize - 1] = '.';
		}
		strncpy(buf + bsize,(const char *)sec,rlen);
		bsize += rlen + 1;
		buf[bsize - 1] = '\0';
		sec += rlen;
		*idx += rlen;
	}
	if(buf == NULL){
		return NULL;
	}
	if(class || type){
		if(len < *idx + 4){
			free(buf);
			return NULL;
		}
		*class = *((uint16_t *)sec + 1);
		*type = *(uint16_t *)sec;
		*idx += 4;
	}
	return buf;
}

static void *
extract_dns_extra(size_t len,const unsigned char *sec,unsigned *ttl,
				unsigned *idx,const unsigned char *orig,
				unsigned type){
	unsigned newidx;
	uint16_t rdlen;
	char *buf;

	*idx = 0;
	if(len < 6u + *idx){
		return NULL;
	}
	*ttl = ntohl(*(const uint32_t *)sec);
	rdlen = ntohs(*((const uint16_t *)sec + 2));
	// FIXME incorrect given possibility of compression!
	if(len < rdlen + 6u + *idx){
		return NULL;
	}
	*idx += 6;
	sec += 6;
	if(type == DNS_TYPE_PTR){
		buf = extract_dns_record(len,sec,NULL,NULL,&newidx,orig);
	}else{
		buf = memdup(sec,rdlen);
	}
	sec += rdlen;
	*idx += rdlen;
	return buf;
}

void handle_dns_packet(const omphalos_iface *octx,omphalos_packet *op,const void *frame,size_t len){
	const struct dnshdr *dns = frame;
	unsigned class,type,bsize;
	const unsigned char *sec;
	uint16_t qd,an,ns,ar;
	char *buf;


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
	// octx->diagnostic("q/a/n/a: %hu/%hu/%hu/%hu",qd,an,ns,ar);
	if(qd){
		buf = extract_dns_record(len,sec,&class,&type,&bsize,frame);
		if(buf == NULL){
			goto malformed;
		}
		free(buf);
		sec += bsize;
		len -= bsize;
	}
	while(an--){
		unsigned ttl;
		char *data;

		buf = extract_dns_record(len,sec,&class,&type,&bsize,frame);
		if(buf == NULL){
			goto malformed;
		}
		// octx->diagnostic("lookup [%s]",buf);
		sec += bsize;
		len -= bsize;
		data = extract_dns_extra(len,sec,&ttl,&bsize,frame,type);
		if(data == NULL){
			free(buf);
			goto malformed;
		}
		if(class == DNS_CLASS_IN){
			int fam;

			if(type == DNS_TYPE_PTR){
				char ss[16]; // FIXME

				if(process_reverse_lookup(buf,&fam,ss)){
					free(buf);
					goto malformed;
				}
				// FIXME perform routing lookup on ss to get
				// the desired interface and see whether we care
				// about this address
				offer_resolution(octx,fam,ss,data);
			}else if(type == DNS_TYPE_A){
				offer_resolution(octx,AF_INET,data,buf);
			}
			//octx->diagnostic("TYPE: %hu CLASS: %hu",
			//		,ntohs(*((uint16_t *)sec + 1)));
		}
		// FIXME handle A/AAAA
		free(buf);
		free(data);
		sec += bsize;
		len -= bsize;
	}
	while(ns--){
		if(len == 0){
			goto malformed;
		}
	}
	while(ar--){
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
