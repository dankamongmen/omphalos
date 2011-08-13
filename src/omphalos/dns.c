#include <stdint.h>
#include <omphalos/dns.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

struct dnshdr {
	uint16_t id;
	uint16_t crap;
	uint16_t qdcount,ancount,nscount,arcount;
	// question, answer, authority, and additional sections follow
};

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
	qd = ntohs(dns->qdcount);
	an = ntohs(dns->ancount);
	ns = ntohs(dns->nscount);
	ar = ntohs(dns->arcount);
	len -= sizeof(*dns);
	sec = (const unsigned char *)frame + sizeof(*dns);
	if(qd){
		char *tmp,*buf = NULL;
		unsigned char rlen;
		unsigned idx = 0;
		size_t bsize = 0;

		while(len > ++idx && (rlen = *sec)){
			if(rlen >= 192 || rlen + idx > len){
				free(buf);
				goto malformed;
			}
			if((tmp = realloc(buf,bsize + rlen + 1)) == NULL){
				free(buf);
				goto malformed;
			}
			buf = tmp;
			if(bsize){
				buf[bsize - 1] = '.';
			}
			strncpy(buf + bsize,(const char *)sec + 1,*sec);
			bsize += rlen + 1;
			idx += *sec;
			sec += *sec + 1;
		}
		if(len <= idx || buf == NULL){
			free(buf);
			goto malformed;
		}
		buf[bsize] = '\0';
		octx->diagnostic("QUESTION: %s",buf);
		// FIXME
		free(buf);
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
