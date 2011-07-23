#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <omphalos/iana.h>
#include <omphalos/inotify.h>
#include <omphalos/ethernet.h>
#include <omphalos/omphalos.h>

#define OUITRIE_SIZE 256

// Two levels point to ouitrie's. The final level points to char *'s.
typedef struct ouitrie {
	void *next[OUITRIE_SIZE];
} ouitrie;

static ouitrie *trie[OUITRIE_SIZE];

static const char *ianafn; // FIXME dangerous

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

static void
parse_file(const omphalos_iface *octx){
	//ouitrie *head[OUITRIE_SIZE];
	unsigned /*z,*/allocerr;
	char buf[256]; // FIXME
	FILE *fp;

	if((fp = fopen(ianafn,"r")) == NULL){
		octx->diagnostic("Coudln't open %s (%s?)",ianafn,strerror(errno));
		return;
	}
	/*
	for(z = 0 ; z < OUITRIE_SIZE ; ++z){
		head[z] = NULL;
	}
	*/
	clearerr(fp);
	allocerr = 0;
	while(fgets(buf,sizeof(buf),fp)){
		unsigned long hex;
		unsigned char b;
		ouitrie *cur,*c;
		char *end,*nl;

		if((hex = strtoul(buf,&end,16)) > ((1u << 24u) - 1)){
			continue;
		}
		if(!isspace(*end) || end == buf){
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
		}
		b = (hex & (0xffu << 8u)) >> 8u;
		if((c = cur->next[b]) == NULL){
			if((c = cur->next[b] = malloc(sizeof(ouitrie))) == NULL){
				break; // FIXME
			}
		}
		b = hex & 0xff;
		if(c->next[b] == NULL){
			if((c->next[b] = strdup(end)) == NULL){
				break; // FIXME
			}
		}
		allocerr = 0;
	}
	if(allocerr){
		octx->diagnostic("Couldn't allocate for %s",ianafn);
		//free_ouitries(head);
	}else if(ferror(fp)){
		octx->diagnostic("Error reading %s",ianafn);
		//free_ouitries(head);
	}/*else{
		free_ouitries(trie);
		memcpy(trie,head,sizeof(head));
	}*/
	fclose(fp);
}

// Load IANA OUI descriptions from the specified file, and watch it for updates
int init_iana_naming(const omphalos_iface *octx,const char *fn){
	ianafn = fn;
	if(watch_file(octx,fn,parse_file)){
		return -1;
	}
	return 0;
}

// FIXME use a trie or bsearch
// FIXME generate data from a text file, preferably one taken from IANA or
// whoever administers the multicast address space
static inline const char *
name_ethmcastaddr(const void *mac){
	static const struct mcast {
		const char *name;
		const char *mac;
		size_t mlen;
	} mcasts[] = {
		{ // FIXME need handle MPLS Multicast on 01:00:53:1+
			.name = "Internet Multicast",
			.mac = "\x01\x00\x5e",
			.mlen = 3,
		},{
			.name = "Spanning Tree Protocol",
			.mac = "\x01\x80\xc2\x00\x00\x00",
			.mlen = 6,
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
const char *iana_lookup(const void *unsafe_oui){
	const unsigned char *oui = unsafe_oui;
	const ouitrie *t;

	if( (t = trie[oui[0]]) ){
		if( (t = t->next[oui[1]]) ){
			return t->next[oui[2]];
		}
	}
	if(categorize_ethaddr(oui) == RTN_MULTICAST){
		return name_ethmcastaddr(oui);
	}
	return NULL;
}

void cleanup_iana_naming(void){
	free_ouitries(trie);
}
