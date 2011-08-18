#include <assert.h>
#include <pthread.h>
#include <omphalos/dns.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/resolv.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>

typedef struct resolvq {
	struct l3host *l3;
	struct l2host *l2;
	struct interface *i;
	struct resolvq *next;
} resolvq;

// Resolv queue is global to all interfaces, since there's no required mapping
// between routes to resolvers and interfaces.
static resolvq *rqueue;
static pthread_mutex_t rqueue_lock = PTHREAD_MUTEX_INITIALIZER;

static inline resolvq *
create_resolvq(struct interface *i,struct l2host *l2,struct l3host *l3){
	resolvq *r;

	if( (r = malloc(sizeof(*r))) ){
		r->l3 = l3;
		r->l2 = l2;
		r->i = i;
		r->next = NULL;
		pthread_mutex_lock(&rqueue_lock);
		r->next = rqueue;
		rqueue = r;
		pthread_mutex_unlock(&rqueue_lock);
	}
	return r;
}

int queue_for_naming(struct interface *i,struct l2host *l2,struct l3host *l3){
	return create_resolvq(i,l2,l3) ? 0 : -1;
}

int offer_resolution(const omphalos_iface *octx,int fam,const void *addr,
				const char *name,namelevel nlevel){
	resolvq *r,**p;

	for(p = &rqueue ; (r = *p) ; p = &r->next){
		if(l3addr_eq_p(r->l3,fam,addr)){
			name_l3host_absolute(octx,r->i,r->l2,r->l3,name,nlevel);
			if(nlevel >= NAMING_LEVEL_REVDNS){
				*p = r->next;
				free(r);
			}
			break;
		}
	}
	return 0;
}

int cleanup_naming(const omphalos_iface *octx){
	resolvq *r;
	int er;

	er = 0;
	while( (r = rqueue) ){
		rqueue = r->next;
		free(r);
		++er;
	}
	octx->diagnostic("%d outstanding resolutions",er);
	if( (er = pthread_mutex_destroy(&rqueue_lock)) ){
		octx->diagnostic("Error destroying resolvq lock (%s)",strerror(er));
	}
	return er;
}
