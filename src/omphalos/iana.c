#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/diag.h>
#include <omphalos/iana.h>
#include <omphalos/util.h>
#include <omphalos/inotify.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>

#define OUITRIE_SIZE 256

// Two levels point to ouitrie's. The final level points to char *'s.
typedef struct ouitrie {
	void *next[OUITRIE_SIZE];
} ouitrie;

static ouitrie *trie[OUITRIE_SIZE];

static void
free_ouitries(ouitrie **tries){
	unsigned z;

	for(z = 0 ; z < OUITRIE_SIZE ; ++z){
		ouitrie *t;
		unsigned y;

		if((t = tries[z]) == NULL){
			continue;
		}
		for(y = 0 ; y < OUITRIE_SIZE ; ++y){
			ouitrie *ty;
			unsigned x;

			if((ty = t->next[y]) == NULL){
				continue;
			}
			for(x = 0 ; x < OUITRIE_SIZE ; ++x){
				free(ty->next[x]);
			}
			free(ty);
		}
		free(t);
		tries[z] = NULL;
	}
}

static int
parse_file(const char *fn){
	unsigned allocerr,count = 0;
	const char *line;
	int l,ret = -1;
	FILE *fp;
	char *b;

	if((fp = fopen(fn,"r")) == NULL){
		diagnostic(L"Coudln't open %s (%s?)",fn,strerror(errno));
		return -1;
	}
	clearerr(fp);
	allocerr = 0;
	b = NULL;
	l = 0;
	while( (line = fgetl(&b,&l,fp)) ){
		unsigned long hex;
		unsigned char b;
		ouitrie *cur,*c;
		char *end,*nl;

		if((hex = strtoul(line,&end,16)) > ((1u << 24u) - 1)){
			continue;
		}
		if(!isspace(*end) || end == line){
			continue;
		}
		while(isspace(*end)){
			++end;
		}
		nl = end;
		while(*nl && *nl != '\n'){
			++nl;
		}
		if(*nl == '\n'){
			*nl = '\0';
		}
		if(nl == end){
			continue;
		}
		b = (hex & (0xffu << 16u)) >> 16u;
		allocerr = 1;
		if((cur = trie[b]) == NULL){
			if((cur = trie[b] = malloc(sizeof(ouitrie))) == NULL){
				break; // FIXME
			}
			memset(cur,0,sizeof(*cur));
		}
		b = (hex & (0xffu << 8u)) >> 8u;
		if((c = cur->next[b]) == NULL){
			if((c = cur->next[b] = malloc(sizeof(ouitrie))) == NULL){
				break; // FIXME
			}
			memset(c,0,sizeof(*c));
		}
		b = hex & 0xff;
		// We can't invalidate the previous entry, to which any number
		// of existing l2hosts might have pointers.
		if(c->next[b] == NULL){
			if((c->next[b] = malloc(sizeof(wchar_t) * (strlen(end) + 1))) == NULL){
				break; // FIXME
			}
			mbstowcs(c->next[b],end,strlen(end) + 1);
		}
		++count;
		allocerr = 0;
	}
	free(b);
	if(allocerr){
		diagnostic(L"Couldn't allocate for %s",fn);
	}else if(ferror(fp)){
		diagnostic(L"Error reading %s",fn);
	}else{
		ret = 0;
	}
	fclose(fp);
	if(count){
		diagnostic(L"Reloaded %u OUI%s from %s",count,
					count == 1 ? "" : "s",fn);
	}
	return ret;
}

// A value can be passed which will be "broadcast" out to all children of this
// node, really useful only for OUI's of size other than the typical 24 bits
// (of which one is the multicast bit) such as IPv6 multicast space.
static ouitrie *
make_oui(const wchar_t *broadcast){
	ouitrie *o;

	if( (o = malloc(sizeof(*o))) ){
		unsigned z;

		for(z = 0 ; z < OUITRIE_SIZE ; ++z){
			if(broadcast){
				if((o->next[z] = wcsdup(broadcast)) == NULL){
					while(z--){
						free(o->next[z]);
					}
					free(o);
					return NULL;
				}
			}else{
				o->next[z] = NULL;
			}
		}
	}
	return o;
}

// Load IANA OUI descriptions from the specified file, and watch it for updates
int init_iana_naming(const char *fn){
	ouitrie *path,*p;
	wchar_t *w;

	if(((p = make_oui(NULL)) == NULL)){
		return -1;
	}
	if((path = make_oui(L"RFC 2464 IPv6 multicast")) == NULL){
		free_ouitries(&p);
		return -1;
	}
	if((w = wcsdup(L"RFC 4862 IPv6 link-local solicitation")) == NULL){
		free_ouitries(&p);
		free_ouitries(&path);
		return -1;

	}
	free(path->next[0xff]);
	path->next[0xff] = w;
	trie[0x33] = p;
	p->next[0x33] = path;
	if(watch_file(fn,parse_file)){
		free_ouitries(trie);
		return -1;
	}
	return 0;
}

// FIXME use the main IANA trie, making it varying-length so we can do longest-
// match. FIXME generate data from a text file, preferably one taken from IANA
// or whoever administers the multicast address space
static inline const wchar_t *
name_ethmcastaddr(const void *mac){
	static const struct mcast {
		const wchar_t *name;
		const char *mac;
		size_t mlen;
		uint16_t eproto;	// host byte order
	} mcasts[] = {
		// We don't list eg mDNS because the 224.0.0.0/4 network
		// is larger than the 23 bits available for mapping, and
		// thus other multicast addresses could use that MAC.
		{ // FIXME need handle MPLS Multicast on 01:00:53:1+
			.name = L"RFC 1112 IPv4 multicast",
			.mac = "\x01\x00\x5e",	// low order 23 bits of ip addresses from 224.0.0.0/4
			.mlen = 3,
			.eproto = ETH_P_IP,
		},{
			.name = L"802.1s Shared Spanning Tree Protocol",
			.mac = "\x01\x00\x0c\xcc\xcc\xcd",
			.mlen = 6,
			.eproto = ETH_P_STP, // FIXME verify
		},{
			.name = L"802.1d Spanning Tree Protocol",
			.mac = "\x01\x80\xc2\x00\x00\x00",
			.mlen = 6,
			// STP actually almost always goes over 802.2 with a
			// SAP value of 0x42, rather than Ethernet II.
			.eproto = ETH_P_STP,
		},{
			.name = L"802.3ah Ethernet OAM",
			.mac = "\x01\x80\xc2\x00\x00\x02",
			.mlen = 6,
			.eproto = ETH_P_SLOW,
		},{
			.name = L"802.1ad Provider bridge STP",
			.mac = "\x01\x80\xc2\x00\x00\x08",
			.mlen = 6,
			.eproto = ETH_P_STP,
		},{
			.name = L"FDDI RMT directed beacon",
			.mac = "\x01\x80\xc2\x00\x10\x00",
			.mlen = 6,
			.eproto = ETH_P_STP,
		},{
			.name = L"FDDI status report frame",
			.mac = "\x01\x80\xc2\x00\x10\x10",
			.mlen = 6,
			.eproto = ETH_P_STP,
		},{
			.name = L"DEC Maintenance Operation Protocol",
			.mac = "\xab\x00\x00\x02\x00\x00",
			.mlen = 6,
			.eproto = ETH_P_DNA_RC,
		},{
			.name = L"Ethernet Configuration Test Protocol",
			.mac = "\xcf\x00\x00\x00\x00\x00",
			.mlen = 6,
			.eproto = ETH_P_CTP,
		},{ .name = NULL, .mac = NULL, .mlen = 0, }
	},*mc;

	for(mc = mcasts ; mc->name ; ++mc){
		if(memcmp(mac,mc->mac,mc->mlen) == 0){
			return mc->name;
		}
	}
	return NULL;
}

// Look up the 24-bit OUI against IANA specifications.
const wchar_t *iana_lookup(const void *unsafe_oui,size_t addrlen){
	const unsigned char *oui = unsafe_oui;
	const ouitrie *t;

	assert(addrlen == ETH_ALEN);
	if( (t = trie[oui[0]]) ){
		if( (t = t->next[oui[1]]) ){
			return t->next[oui[2]];
		}
	}
	if(categorize_ethaddr(oui) == RTN_MULTICAST){
		return name_ethmcastaddr(oui);
	}
	if(oui[0] & 0x02){
		return L"IEEE 802 locally-assigned MAC";
	}
	return NULL;
}

void cleanup_iana_naming(void){
	free_ouitries(trie);
}
