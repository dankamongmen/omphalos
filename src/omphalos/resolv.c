#include <assert.h>
#include <pthread.h>
#include <omphalos/dns.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/resolv.h>
#include <omphalos/netaddrs.h>

typedef struct resolvq {
	struct l3host *l3;
	struct resolvq *prev,*next;
} resolvq;

// Resolv queue is global to all interfaces, since there's no required mapping
// between routes to resolvers and interfaces.
static resolvq *rqueue;
static pthread_mutex_t rqueue_lock = PTHREAD_MUTEX_INITIALIZER;

static resolvq *
create_resolvq(struct l3host *l3){
	resolvq *r;

	if( (r = malloc(sizeof(*r))) ){
		r->l3 = l3;
		pthread_mutex_lock(&rqueue_lock);
		if( (r->next = rqueue) ){
			rqueue->prev->next = r;
			r->prev = rqueue->prev;
			rqueue->prev = r;
		}else{
			r->prev = r;
		}
		rqueue = r;
		pthread_mutex_unlock(&rqueue_lock);
	}
	return r;
}

void queue_for_naming(struct l3host *l3){
	create_resolvq(l3);
}

void cleanup_naming(const omphalos_iface *octx){
	resolvq *r;
	int er;

	if( (er = pthread_mutex_destroy(&rqueue_lock)) ){
		octx->diagnostic("Error destroying resolvq lock (%s)",strerror(er));
	}
	er = 0;
	if( (r = rqueue) ){
		r->prev->next = NULL;
		do{
			rqueue = r->next;
			free(r);
			++er;
		}while( (r = rqueue) );
	}
	octx->diagnostic("%d outstanding resolutions",er);
}
