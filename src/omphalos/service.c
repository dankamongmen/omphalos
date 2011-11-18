#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <omphalos/diag.h>
#include <omphalos/service.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>

typedef struct l4srv {
	unsigned proto,port;
	wchar_t *srv,*srvver;		// srvver might be NULL
	struct l4srv *next;
	void *opaque;			// callback state
} l4srv;

static l4srv *
new_service(unsigned proto,unsigned port,const wchar_t *srv,const wchar_t *srvver){
	l4srv *r;

	if( (r = malloc(sizeof(*r))) ){
		r->srvver = NULL;
		if(!srvver ||  (r->srvver = wcsdup(srvver)) ){
			if( (r->srv = wcsdup(srv)) ){
				r->opaque = NULL;
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

void observe_service(struct interface *i,struct l2host *l2,struct l3host *l3,
			unsigned proto,unsigned port,
			const wchar_t *srv,const wchar_t *srvver){
	const omphalos_ctx *octx = get_octx();
	l4srv *services,*curs;

	services = l3_getservices(l3);
	for(curs = services ; curs ; curs = curs->next){
		if(curs->proto == proto && curs->port == port && wcscmp(curs->srv,srv) == 0){
			return;
		}
	}
	curs = new_service(proto,port,srv,srvver);
	curs->next = services;
	l3_setservices(l3,curs);
	if(octx->iface.srv_event){
		octx->iface.srv_event(i,l2,l3,curs);
	}
}

// Destroy a services structure.
void free_services(l4srv *l){
	l4srv *tmp;

	while( (tmp = l) ){
		l = l->next;
		free_service(tmp);
	}
}

const wchar_t *l4srvstr(const l4srv *l){
	return l->srv;
}

void *l4host_get_opaque(l4srv *l4){
	return l4->opaque;
}
