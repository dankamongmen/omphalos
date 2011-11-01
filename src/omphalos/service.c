#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <omphalos/service.h>
#include <omphalos/netaddrs.h>

typedef struct l4srv {
	unsigned proto,port;
	char *srv,*srvver;
	struct l4srv *next;
} l4srv;

static l4srv *
new_service(unsigned proto,unsigned port,const char *srv,const char *srvver){
	l4srv *r;

	if( (r = malloc(sizeof(*r))) ){
		if( (r->srvver = strdup(srvver)) ){
			if( (r->srv = strdup(srv)) ){
				r->proto = proto;
				r->port = port;
				return r;
			}
			free(r->srvver);
		}
		free(r);
	}
	return NULL;
}

static inline void
free_service(l4srv *l){
	if(l){
		free(l->srvver);
		free(l->srv);
		free(l);
	}
}

void observe_service(struct l3host *l3,unsigned proto,unsigned port,
			const char *srv,const char *srvver){
	l4srv *services,*curs;

	services = l3_getservices(l3);
	for(curs = services ; curs ; curs = curs->next){
		if(curs->proto == proto && curs->port == port){
			return;
		}
	}
	curs = new_service(proto,port,srv,srvver);
	free_service(curs); // FIXME do something with new service entry
}
