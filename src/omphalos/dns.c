#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <linux/udp.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <omphalos/ip.h>
#include <omphalos/tx.h>
#include <omphalos/dns.h>
#include <omphalos/udp.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/csum.h>
#include <omphalos/route.h>
#include <omphalos/resolv.h>
#include <omphalos/service.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define IP4_REVSTR_DECODED ".in-addr.arpa"
#define IP4_REVSTR "\x07" "in-addr" "\x04" "arpa"
#define IP6_REVSTR_DECODED ".ip6.arpa"
#define IP6_REVSTR "\x03" "ip6" "\x04" "arpa"
#define MDNS_REVSTR_DECODED ".local"
#define MDNS_REVSTR "\x05" "local"

// Mask against the flags field of the dnshdr struct
#define RESPONSE_CODE_MASK 0x7
enum {
	RESPONSE_CODE_OK = 0,
	RESPONSE_CODE_FORMAT = 1,
	RESPONSE_CODE_SERVER = 2,
	RESPONSE_CODE_NXDOMAIN = 3,
	RESPONSE_CODE_NOTIMPLEMENTED = 4,
	RESPONSE_CODE_REFUSED = 5,
	// FIXME more...
} response_codes;

#define DNS_TARGET_PORT 53	// FIXME terrible

// Encoded as 32 nibbles. len is the length prior to the (already-verified)
// instance of IP6_REVSTR, and thus ought be exactly 63 octets (32 nibbles and
// 31 delimiters).
static int
process_reverse6_lookup(const char *buf,int *fam,void *addr,size_t len){
	uint128_t addr6;
	int wantdigit;

	if(len != 63){
		return -1;
	}
	memset(&addr6,0,sizeof(addr6));
	wantdigit = 1;
	while(len){
		if(wantdigit){
			unsigned val;

			if(!isxdigit(*buf)){
				return -1;
			}
			val = strtoul(buf,NULL,16);
			addr6[len / 16] = htonl(ntohl(addr6[len / 16]) | (val << (28 - ((len % 16) / 2 * 4))));
		}else{
			if(*buf != '.'){
				return -1;
			}
		}
		++buf;
		--len;
		wantdigit = !wantdigit;
	}
	*fam = AF_INET6;
	memcpy(addr,&addr6,sizeof(addr6));
	return 0;
}

#define UDP_SRV "_udp."
#define TCP_SRV "_tcp."
#define SDUDP_SRV "_dns-sd._udp."
static size_t
match_srv_proto(const char *buf,unsigned *prot,int *add){
	size_t ret;

	if(strncmp(buf,UDP_SRV,strlen(UDP_SRV)) == 0){
		*prot = IPPROTO_UDP;
		ret = strlen(UDP_SRV);
		*add = 1;
	}else if(strncmp(buf,TCP_SRV,strlen(TCP_SRV)) == 0){
		*prot = IPPROTO_TCP;
		ret = strlen(TCP_SRV);
		*add = 1;
	}else if(strncmp(buf,SDUDP_SRV,strlen(SDUDP_SRV)) == 0){
		*prot = IPPROTO_UDP;
		ret = strlen(SDUDP_SRV);
	}else{
		ret = 0;
	}
	return ret;
}
#undef SDUDP_SRV
#undef UDP_SRV
#undef TCP_SRV

#define LOCAL_DOMAIN "local"
static wchar_t *
process_srv_lookup(const char *buf,unsigned *prot,unsigned *port,int *add){
	size_t nlen,pconv,tlen = 64;
	const char *srv,*domain;
	wchar_t *name;
	int conv;

	*add = 0;
	if((name = malloc(sizeof(*name) * tlen)) == NULL){
		return NULL;
	}
	nlen = 0;
	while((pconv = match_srv_proto(buf,prot,add)) == 0){
		nlen = 0;
		if(*buf == '_'){
			++buf;
		}
		srv = buf;
		while(*buf != '.' && (conv = mbtowc(&name[nlen],buf,MB_CUR_MAX)) >= 0){
			buf += conv;
			if(++nlen >= tlen - 1){
				free(name);
				return NULL; // FIXME need grow it
			}
		}
		if(*buf != '.' || buf == srv){
			free(name);
			return NULL;
		}
		// see the test above; there's always space guaranteed us
		if(mbtowc(name + nlen++,buf++,1) != 1){
			free(name);
			return NULL;
		}
	}
	if(nlen == 0){
		free(name);
		return NULL;
	}
	name[nlen - 1] = L'\0'; // always space; write over last '.'
	buf += pconv;
	// FIXME sometimes we have four-part names, and not just SD*_SRV
	domain = buf;
	if(strcmp(domain,LOCAL_DOMAIN)){
		free(name);
		return NULL;
	}
	*port = 0; // FIXME
	return name;
}
#undef LOCAL_DOMAIN

// FIXME is it safe to be using (possibly signed) naked chars?
static int
process_reverse_lookup(const char *buf,int *fam,void *addr){
	const size_t len = strlen(buf);
	char obuf[INET6_ADDRSTRLEN];
	const char *chunk,*c;
	unsigned quad;
	char q[4][5];

	// First, check for mDNS
	if(len < __builtin_strlen(MDNS_REVSTR_DECODED)){
		return -1;
	}
	const size_t xmlen = len - __builtin_strlen(MDNS_REVSTR_DECODED);
	if(strcmp(buf + xmlen,MDNS_REVSTR_DECODED) == 0){
		// FIXME for now, do nothing with mDNS PTR records
		return -1;
	}
	// Check the IPv6 string first (it's shorter)
	if(len < __builtin_strlen(IP6_REVSTR_DECODED)){
		return -1;
	}
	const size_t x6len = len - __builtin_strlen(IP6_REVSTR_DECODED);
	if(strcmp(buf + x6len,IP6_REVSTR_DECODED) == 0){
		return process_reverse6_lookup(buf,fam,addr,x6len);
	}
	// Look for the IPv4 string
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
		if(rlen == 0){
			buf = strdup("");
		}
		if(buf == NULL){
			return NULL;
		}
	}
	if(class || type){
		if(len < *idx + 4){
			free(buf);
			return NULL;
		}
		*class = *((uint16_t *)sec + 1) & ~(DNS_CLASS_FLUSH);
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

// Returns 1 if answers were successfully extracted, 0 otherwise for valid
// queries, and -1 on error. Success is carried by the 'server' boolean.
int handle_dns_packet(omphalos_packet *op,const void *frame,size_t len){
	const struct dnshdr *dns = frame;
	uint16_t qd,an,ns,ar,flags;
	unsigned class,type,bsize;
	const unsigned char *sec;
	union {
		uint128_t addr6;
		uint32_t addr4;
	} nsaddru;
	int server = 0;
	void *nsaddr;
	char *buf;
	int nsfam;

	if(len < sizeof(*dns)){
		goto malformed;
	}
	if(op->l3proto == ETH_P_IP){
		nsfam = AF_INET;
		nsaddr = &nsaddru.addr4;
		memcpy(nsaddr,op->l3saddr,4);
	}else if(op->l3proto == ETH_P_IPV6){
		nsfam = AF_INET6;
		nsaddr = &nsaddru.addr6;
		memcpy(nsaddr,op->l3saddr,16);
	}else{
		diagnostic(L"DNS on %s:0x%x",op->i->name,op->l3proto);
		op->noproto = 1;
		return 0;
	}
	//opcode = (ntohs(dns->flags) & 0x7800) >> 11u;
	qd = ntohs(dns->qdcount);
	an = ntohs(dns->ancount);
	ns = ntohs(dns->nscount);
	ar = ntohs(dns->arcount);
	flags = ntohs(dns->flags);
	len -= sizeof(*dns);
	sec = (const unsigned char *)frame + sizeof(*dns);
	//diagnostic(L"q/a/n/a: %hu/%hu/%hu/%hu",qd,an,ns,ar);
	while(qd && len){
		buf = extract_dns_record(len,sec,&class,&type,&bsize,frame);
		if(buf == NULL){
			goto malformed;
		}
		if((flags & RESPONSE_CODE_MASK) == RESPONSE_CODE_NXDOMAIN){
			if(class == DNS_CLASS_IN){
				server = 1;
				if(type == DNS_TYPE_PTR){
					char ss[16]; // FIXME
					int fam;

					if(process_reverse_lookup(buf,&fam,ss) == 0){
						// FIXME perform routing lookup on ss to get
						// the desired interface and see whether we care
						// about this address
						offer_wresolution(fam,ss,L"address unknown",
							NAMING_LEVEL_NXDOMAIN,nsfam,nsaddr);
					}
				}
			}
		}
		free(buf);
		sec += bsize;
		len -= bsize;
		--qd;
	}
	while(an && len){
		unsigned ttl;
		char *data;

		buf = extract_dns_record(len,sec,&class,&type,&bsize,frame);
		if(buf == NULL){
			goto malformed;
		}
		//diagnostic(L"lookup [%s]",buf);
		sec += bsize;
		len -= bsize;
		data = extract_dns_extra(len,sec,&ttl,&bsize,frame,type);
		if(data == NULL){
			free(buf);
			goto malformed;
		}
		if(class == DNS_CLASS_IN){
			int fam;

			server = 1;
			if(type == DNS_TYPE_PTR){
				unsigned proto,port;
				wchar_t *srv;
				char ss[16]; // FIXME
				int add;

				// A failure here doesn't mean the response is
				// malformed, necessarily, but simply that it
				// wasn't for an address (mDNS SD does this).
				if(process_reverse_lookup(buf,&fam,ss) == 0){
					// FIXME perform routing lookup on ss to get
					// the desired interface and see whether we care
					// about this address
					offer_resolution(fam,ss,data,
						NAMING_LEVEL_REVDNS,nsfam,nsaddr);
				}else if( (srv = process_srv_lookup(buf,&proto,&port,&add)) ){;
					if(add){
						observe_service(op->i,op->l2s,op->l3s,proto,port,srv,NULL);
					}
					free(srv);
				}else{
					free(buf);
					goto malformed;
				}
			}else if(type == DNS_TYPE_A){
				offer_resolution(AF_INET,data,buf,
						NAMING_LEVEL_DNS,nsfam,nsaddr);
			}else if(type == DNS_TYPE_AAAA){
				offer_resolution(AF_INET6,data,buf,
						NAMING_LEVEL_DNS,nsfam,nsaddr);
			}else if(type == DNS_TYPE_CNAME){
				// FIXME do what (nothing probably)?
			}else if(type == DNS_TYPE_TXT){
				// FIXME do what?
			}else if(type == DNS_TYPE_SRV){
				unsigned proto,port;
				wchar_t *srv;
				int add;

				if( (srv = process_srv_lookup(buf,&proto,&port,&add)) ){
					if(add){
						observe_service(op->i,op->l2s,op->l3s,proto,port,srv,NULL);
					}
					free(srv);
				}else{
					free(buf);
					goto malformed;
				}
			}else if(type == DNS_TYPE_HINFO){
				// FIXME do what?
			}
			//diagnostic(L"TYPE: %hu CLASS: %hu",
			//		,ntohs(*((uint16_t *)sec + 1)));
		}
		free(buf);
		free(data);
		sec += bsize;
		len -= bsize;
		--an;
	}
	len = ns = ar = 0; // FIXME learn how to parse ns/ar
	/* FIXME while(ns && len){
		--ns;
	}
	while(ar && len){
		--ar;
	}*/
	if(ar || ns || an || qd || len){
		goto malformed;
	}
	return server;

malformed:
	diagnostic(L"%s malformed with %zu on %s",__func__,len,op->i->name);
	op->malformed = 1;
	return -1;
}

int tx_dns_ptr(int fam,const void *addr,const char *question){
	struct routepath rp;
	void *frame;
	size_t flen;
	int r;

	assert(fam == AF_INET || fam == AF_INET6);
	if(get_router(fam,addr,&rp)){
		return -1;
	}
	if((frame = get_tx_frame(rp.i,&flen)) == NULL){
		return -1;
	}
	r = setup_dns_ptr(&rp,fam,DNS_TARGET_PORT,flen,frame,question);
	if(r){
		abort_tx_frame(rp.i,frame);
		return -1;
	}
	send_tx_frame(rp.i,frame);
	return 0;
}

int setup_dns_ptr(const struct routepath *rp,int fam,unsigned port,
			size_t flen,void *frame,const char *question){
	struct tpacket_hdr *thdr;
	uint16_t *totlen,tptr;
	struct dnshdr *dnshdr;
	struct udphdr *udp;
	hwaddrint hw;
	size_t tlen;
	void *iphdr;
	int r;

	hw = get_hwaddr(rp->l2);
	thdr = frame;
	tlen = thdr->tp_mac;
	if((r = prep_eth_header((char *)frame + tlen,flen - tlen,rp->i,&hw,
				fam == AF_INET ? ETH_P_IP : ETH_P_IPV6)) < 0){
		return -1;
	}
	tlen += r;
	iphdr = (char *)frame + tlen;
	// Stash the <l3 headers' total size, so we can set tot_len when done
	if(fam == AF_INET){
		uint32_t addr4 = get_l3addr_in(rp->l3);
		uint32_t src4 = rp->src[0];

		totlen = &((struct iphdr *)iphdr)->tot_len;
		r = prep_ipv4_header(iphdr,flen - tlen,src4,addr4,IPPROTO_UDP);
		*totlen = tlen;
	}else if(fam == AF_INET6){
		uint128_t addr6;
		uint128_t src6;

		memcpy(&addr6,get_l3addr_in6(rp->l3),sizeof(addr6));
		memcpy(&src6,&rp->src,sizeof(src6));
		totlen = &((struct ip6_hdr *)iphdr)->ip6_ctlun.ip6_un1.ip6_un1_plen;
		r = prep_ipv6_header(iphdr,flen - tlen,src6,addr6,IPPROTO_UDP);
		*totlen = tlen + r;
	}else{
		return -1;
	}
	if(r < 0){
		return -1;
	}
	tlen += r;
	if(flen - tlen < sizeof(*udp)){
		return -1;
	}
	udp = (struct udphdr *)((char *)frame + tlen);
	udp->dest = htons(port);
	// Don't send from the mDNS full resolver port or anything below. gross
	udp->source = htons(5353);//htons((random() % 60000) + 5354);
	udp->check = 0u;
	tlen += sizeof(*udp);
	if(flen - tlen < sizeof(*dnshdr) + strlen(question) + 1 + 4){
		return -1;
	}
	dnshdr = (struct dnshdr *)((char *)frame + tlen);
	dnshdr->id = random();
	dnshdr->flags = htons(0x0100u);
	dnshdr->qdcount = htons(1);
	dnshdr->ancount = 0;
	dnshdr->nscount = 0;
	dnshdr->arcount = 0;
	tlen += sizeof(struct dnshdr);
	udp->len = sizeof(struct udphdr) + sizeof(struct dnshdr);
	strcpy((char *)frame + tlen,question);
	tlen += strlen(question) + 1;
	tptr = DNS_TYPE_PTR;
	memcpy((char *)frame + tlen,&tptr,2);
	tptr = DNS_CLASS_IN;
	memcpy((char *)frame + tlen + 2,&tptr,2);
	tlen += 4;
	thdr->tp_len = tlen - sizeof(*thdr);
	udp->len += strlen(question) + 1 + 4;
	udp->len = htons(udp->len);
	*totlen = htons(tlen - *totlen);
	if(fam == AF_INET){
		((struct iphdr *)iphdr)->check = ipv4_csum(iphdr);
		udp->check = udp4_csum(iphdr);
	}else{
		udp->check = udp6_csum(iphdr);
	}
	return 0;
}

char *rev_dns_a(const void *i4){
	size_t l = INET_ADDRSTRLEN + strlen(IP4_REVSTR) + 1;
	const uint32_t ip = *(const uint32_t *)i4;
	char *buf;

	if( (buf = malloc(l)) ){
		uint32_t mask;
		unsigned shr;
		int r = 0;

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
	// 32 single-digit nibbles, 32 dots, and IP6_REVSTR. Fixed length.
	size_t l = 32 + 32 + strlen(IP6_REVSTR) + 1;
	char *buf;

	if( (buf = malloc(l)) ){
		unsigned z;

		for(z = 0 ; z < 4 ; ++z){
			const uint32_t ip = ntohl(((const uint32_t *)i6)[(3 - z)]);
			uint32_t mask = 0xf,shr = 0;
			unsigned y;

			for(y = 0 ; y < 32 / 4 ; ++y){
				sprintf(&buf[z * 16 + y * 2],"\x01%x",(ip >> shr) & mask);
				shr += 4;
			}
		}
		sprintf(buf + 64,IP6_REVSTR);
	}
	return buf;
}
