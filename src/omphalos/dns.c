#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <omphalos/tx.h>
#include <omphalos/ip.h>
#include <omphalos/dns.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/route.h>
#include <omphalos/resolv.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define IP4_REVSTR_DECODED ".in-addr.arpa"
#define IP4_REVSTR "\x07" "in-addr" "\x04" "arpa"

#define DNS_CLASS_IN	__constant_ntohs(1u)
#define DNS_TYPE_A	__constant_ntohs(1u)
#define DNS_TYPE_PTR	__constant_ntohs(12u)
#define DNS_TYPE_AAAA	__constant_ntohs(28u)

#define DNS_TARGET_PORT 53	// FIXME terrible

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

	if(len < __builtin_strlen(IP4_REVSTR_DECODED)){
		return -1;
	}
	const size_t xlen = len - __builtin_strlen(IP4_REVSTR_DECODED);
	if(strcmp(buf + xlen,IP4_REVSTR_DECODED)){
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
			ptrdiff_t offset;

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
	union {
		uint128_t addr6;
		uint32_t addr4;
	} nsaddru;
	void *nsaddr = &nsaddru;
	char *buf;
	int nsfam;

	if(len < sizeof(*dns)){
		octx->diagnostic("%s malformed with %zu on %s",
				__func__,len,op->i->name);
		++op->i->malformed;
		return;
	}
	if(op->l3proto == ETH_P_IP){
		nsfam = AF_INET;
		nsaddru.addr4 = get_l3addr_in(op->l3s);
	}else if(op->l3proto == ETH_P_IPV6){
		nsfam = AF_INET6;
		nsaddru.addr6 = get_l3addr_in6(op->l3s);
	}else{
		octx->diagnostic("DNS on %s:0x%x",op->i->name,op->l3proto);
		++op->i->noprotocol;
		return;
	}
	//opcode = (ntohs(dns->flags) & 0x7800) >> 11u;
	qd = ntohs(dns->qdcount);
	an = ntohs(dns->ancount);
	ns = ntohs(dns->nscount);
	ar = ntohs(dns->arcount);
	len -= sizeof(*dns);
	sec = (const unsigned char *)frame + sizeof(*dns);
	// octx->diagnostic("q/a/n/a: %hu/%hu/%hu/%hu",qd,an,ns,ar);
	while(qd--){
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
		//octx->diagnostic("lookup [%s]",buf);
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

				// A failure here doesn't mean the response is
				// malformed, necessarily, but simply that it
				// wasn't for an address (mDNS SD does this).
				if(process_reverse_lookup(buf,&fam,ss) == 0){
					// FIXME perform routing lookup on ss to get
					// the desired interface and see whether we care
					// about this address
					offer_resolution(octx,fam,ss,data,
						NAMING_LEVEL_REVDNS,nsfam,nsaddr);
				}
			}else if(type == DNS_TYPE_A){
				offer_resolution(octx,AF_INET,data,buf,
						NAMING_LEVEL_DNS,nsfam,nsaddr);
			}else if(type == DNS_TYPE_AAAA){
				offer_resolution(octx,AF_INET6,data,buf,
						NAMING_LEVEL_DNS,nsfam,nsaddr);
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

void tx_dns_a(const omphalos_iface *octx,int fam,const void *addr,
		const char *question){
	struct tpacket_hdr *thdr;
	struct dnshdr *dnshdr;
	struct routepath rp;
	struct udphdr *udp;
	size_t flen,tlen;
	uint16_t *totlen;
	hwaddrint hw;
	void *frame;
	int r;

	if(get_router(fam,addr,&rp)){
		return;
	}
	if((frame = get_tx_frame(octx,rp.i,&flen)) == NULL){
		return;
	}
	hw = get_hwaddr(rp.l2);
	thdr = frame;
	tlen = sizeof(*thdr);
	if((r = prep_eth_header(frame + tlen,flen - tlen,rp.i,&hw,ETH_P_IP)) < 0){
		abort_tx_frame(rp.i,frame);
		return;
	}
	tlen += r;
	if(fam == AF_INET){
		uint32_t addr4 = *(const uint32_t *)addr;
		uint32_t src4 = rp.src[0];

		totlen = &((struct iphdr *)(frame + tlen))->tot_len;
		r = prep_ipv4_header(frame + tlen,flen - tlen,src4,addr4,IPPROTO_UDP);
	}else if(fam == AF_INET6){
		// FIXME
	}
	if(r < 0){
		abort_tx_frame(rp.i,frame);
		return;
	}
	// Stash the l2 headers' total size, so we can set tot_len when done
	*totlen = tlen - sizeof(*thdr);
	tlen += r;
	if(flen - tlen < sizeof(*udp)){
		abort_tx_frame(rp.i,frame);
		return;
	}
	udp = (struct udphdr *)((char *)frame + tlen);
	udp->dest = htons(DNS_TARGET_PORT);
	udp->source = 31337; // FIXME lol
	tlen += sizeof(*udp);
	if(flen - tlen < sizeof(*dnshdr) + strlen(question) + 1 + 4){
		abort_tx_frame(rp.i,frame);
		return;
	}
	dnshdr = (struct dnshdr *)((char *)frame + tlen);
	dnshdr->id = 0;
	dnshdr->flags = 0;
	dnshdr->qdcount = htons(1);
	dnshdr->ancount = 0;
	dnshdr->nscount = 0;
	dnshdr->arcount = 0;
	tlen += sizeof(struct dnshdr);
	udp->len = sizeof(struct udphdr) + sizeof(struct dnshdr);
	memcpy((char *)frame + tlen,question,strlen(question) + 1);
	tlen += strlen(question) + 1;
	*(uint16_t *)((char *)frame + tlen + 1) = ntohs(DNS_TYPE_PTR);
	*(uint16_t *)((char *)frame + tlen + 1 + 2) = ntohs(DNS_CLASS_IN);
	tlen += 4;
	thdr->tp_len = tlen;
	udp->len += strlen(question) + 1 + 4;
	udp->len = htons(udp->len);
	*totlen = htons(tlen - *totlen);
	send_tx_frame(octx,rp.i,frame);
}

void tx_dns_aaaa(const omphalos_iface *octx,int fam,const void *addr,
		const char *question){
	struct routepath rp;
	void *frame;
	size_t flen;

	octx->diagnostic("Looking up [%s]",question);
	if(get_router(fam,addr,&rp)){
		return;
	}
	if((frame = get_tx_frame(octx,rp.i,&flen)) == NULL){
		return;
	}
	// FIXME set up AAAA question
	send_tx_frame(octx,rp.i,frame);
}

char *rev_dns_a(const void *i4){
	size_t l = INET_ADDRSTRLEN + strlen(IP4_REVSTR) + 1;
	const uint32_t ip = *(const uint32_t *)i4;
	char *buf;

	if( (buf = malloc(l)) ){
		uint32_t mask;
		unsigned shr;
		int r;

		for(mask = 0xff000000u, shr = 24 ; mask ; mask >>= 8u, shr -= 8){
			int r2;

			r2 = sprintf(buf + r,".%u",(ip & mask) >> shr);
			buf[r] = r2 - 1;
			r += r2;
		}
		sprintf(buf + r,IP4_REVSTR);
	}
	return buf;
}

char *rev_dns_aaaa(const void *i6){
	assert(i6);
	return NULL; // FIXME
}
