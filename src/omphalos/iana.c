#include <omphalos/iana.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

#define OUITRIE_SIZE 256

// Two levels point to ouitrie's. The final level points to char *'s.
typedef struct ouitrie {
	void *next[OUITRIE_SIZE];
} ouitrie;

static ouitrie *trie[OUITRIE_SIZE];

static void
parse_file(const omphalos_iface *octx){
	octx->diagnostic("No support yet!");
}

// Load IANA OUI descriptions from the specified file, and watch it for updates
int init_iana_naming(const omphalos_iface *octx,const char *fn){
	if(watch_file(octx,fn,parse_file)){
		return -1;
	}
	return 0;
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
	return NULL;
}
