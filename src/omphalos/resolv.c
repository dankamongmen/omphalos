#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/resolv.h>
#include <omphalos/inotify.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>

typedef struct resolvq {
	struct l3host *l3;
	struct l2host *l2;
	struct interface *i;
	struct resolvq *next;
} resolvq;

typedef struct resolver {
	union {
		struct in_addr ip4;
		struct in6_addr ip6;
	} addr;
	struct resolver *next;
} resolver;

static char *resolvconf_fn;
static resolver *resolvers,*resolvers6;
static pthread_mutex_t resolver_lock = PTHREAD_MUTEX_INITIALIZER;

// Resolv queue is global to all interfaces, since there's no required mapping
// between routes to resolvers and interfaces.
static resolvq *rqueue;
static pthread_mutex_t rqueue_lock = PTHREAD_MUTEX_INITIALIZER;

static resolver *
create_resolver(const void *addr,size_t len){
	resolver *r;

	assert(len <= sizeof(r->addr));
	if( (r = malloc(sizeof(*r))) ){
		memcpy(&r->addr,addr,len);
		r->next = NULL;
	}
	return r;
}

static void
free_resolvers(resolver **r){
	resolver *tmp;

	while( (tmp = *r) ){
		*r = tmp->next;
		free(tmp);
	}
}

static inline resolvq *
create_resolvq(struct interface *i,struct l2host *l2,struct l3host *l3){
	resolvq *r;

	if( (r = malloc(sizeof(*r))) ){
		r->l3 = l3;
		r->l2 = l2;
		r->i = i;
		r->next = NULL;
	}
	return r;
}

// Must already have been pulled from the queue, or never placed on it
static void
free_resolvq(resolvq *r){
	if(r){
		free(r);
	}
}

int queue_for_naming(const struct omphalos_iface *octx,struct interface *i,
			struct l2host *l2,struct l3host *l3,
		       	dnstxfxn dnsfxn,const char *revstr,
			int fam,const void *lookup){
	int ret = 0;
	resolvq *r;

	if((r = create_resolvq(i,l2,l3)) == NULL){
		return -1;
	}
	if(pthread_mutex_lock(&resolver_lock)){
		free_resolvq(r);
		return -1;
	}
	// FIXME round-robin or even use the simple resolv.conf algorithm
	if(resolvers){
		ret = dnsfxn(octx,AF_INET,&resolvers->addr.ip4,revstr);
	}
	if(resolvers6){
		ret = dnsfxn(octx,AF_INET6,&resolvers6->addr.ip6,revstr);
	}
	pthread_mutex_unlock(&resolver_lock);
	pthread_mutex_lock(&rqueue_lock);
	if(!ret){
		r->next = rqueue;
		rqueue = r;
	}else{
		// FIXME put it on a fail queue and retry when we have a route
		free_resolvq(r);
		r = NULL;
	}
	pthread_mutex_unlock(&rqueue_lock);
	ret |= tx_mdns_ptr(octx,i,revstr,fam,lookup);
	return ret;
}

/*static void
offer_nameserver(int nsfam,const void *nameserver){
	resolver **head,*r;
	size_t len;

	// FIXME don't accept a nameserver to which we can't route, in general
	pthread_mutex_lock(&resolver_lock);
	if(nsfam == AF_INET){
		head = &resolvers;
		len = 4;
	}else if(nsfam == AF_INET6){
		head = &resolvers6;
		len = 16;
	}else{
		pthread_mutex_unlock(&resolver_lock);
		return;
	}
	for(r = *head ; r ; r = r->next){
		if(memcmp(&r->addr,nameserver,len) == 0){
			break;
		}
	}
	if(r == NULL && (r = create_resolver(nameserver,len)) ){
		r->next = *head;
		*head = r;
	}
	pthread_mutex_unlock(&resolver_lock);
}*/

int offer_resolution(const omphalos_iface *octx,int fam,const void *addr,
				const char *name,namelevel nlevel,
				int nsfam,const void *nameserver){
	wchar_t *wname;
	size_t len;
	int r;

	len = strlen(name);
	if((wname = malloc((len + 1) * sizeof(*wname))) == NULL){
		return -1;
	}
	assert(mbsrtowcs(wname,&name,len,NULL) == len);
	wname[len] = L'\0';
	r = offer_wresolution(octx,fam,addr,wname,nlevel,nsfam,nameserver);
	free(wname);
	return r;
}

int offer_wresolution(const omphalos_iface *octx,int fam,const void *addr,
				const wchar_t *name,namelevel nlevel,
				int nsfam,const void *nameserver){
	resolvq *r,**p;

	// FIXME don't call until we filter offers better
	// offer_nameserver(nsfam,nameserver);
	pthread_mutex_lock(&rqueue_lock);
	for(p = &rqueue ; (r = *p) ; p = &r->next){
		if(l3addr_eq_p(r->l3,fam,addr)){
			// FIXME needs to lock the interface to touch l3 objs
			wname_l3host_absolute(octx,r->i,r->l2,r->l3,name,nlevel);
			if(nlevel >= NAMING_LEVEL_REVDNS){
				*p = r->next;
				free(r);
			}
			break;
		}
	}
	pthread_mutex_unlock(&rqueue_lock);
	if(r){
		char abuf[INET6_ADDRSTRLEN],rbuf[INET6_ADDRSTRLEN];

		inet_ntop(fam,addr,abuf,sizeof(abuf));
		inet_ntop(nsfam,nameserver,rbuf,sizeof(rbuf));
		octx->diagnostic(L"Resolved %s @%s as %s",abuf,rbuf,name);
	}
	return 0;
}

static void
parse_resolv_conf(const omphalos_iface *octx){
	resolver *revs = NULL;
	unsigned count = 0;
	char *line;
	FILE *fp;
	char *b;
	int l;

	if((fp = fopen(resolvconf_fn,"r")) == NULL){
		octx->diagnostic(L"Couldn't open %s",resolvconf_fn);
		return;
	}
	b = NULL;
	l = 0;
	errno = 0;
	while( (line = fgetl(&b,&l,fp)) ){
		struct in_addr ina;
		resolver *r;
		char *nl;

#define NSTOKEN "nameserver"
		while(isspace(*line)){
			++line;
		}
		if(*line == '#' || !*line){
			continue;
		}
		if(strncmp(line,NSTOKEN,__builtin_strlen(NSTOKEN))){
			continue;
		}
		line += __builtin_strlen(NSTOKEN);
		if(!isspace(*line)){
			continue;
		}
		do{
			++line;
		}while(isspace(*line));
		nl = strchr(line,'\n');
		*nl = '\0';
		if(inet_pton(AF_INET,line,&ina) != 1){
			continue;
		}
		if((r = create_resolver(&ina,sizeof(ina))) == NULL){
			break; // FIXME
		}
		r->next = revs;
		revs = r;
		++count;
		// FIXME
		//
#undef NSTOKEN
	}
	free(b);
	fclose(fp);
	if(errno){
		free_resolvers(&revs);
	}else{
		resolver *r;

		pthread_mutex_lock(&resolver_lock);
		r = resolvers;
		resolvers = revs;
		pthread_mutex_unlock(&resolver_lock);
		free_resolvers(&r);
		octx->diagnostic(L"Reloaded %u resolver%s from %s",count,
				count == 1 ? "" : "s",resolvconf_fn);
	}
}

int init_naming(const omphalos_iface *octx,const char *resolvconf){
	if((resolvconf_fn = strdup(resolvconf)) == NULL){
		goto err;
	}
	if(watch_file(octx,resolvconf,parse_resolv_conf)){
		goto err;
	}
	return 0;

err:
	free(resolvconf_fn);
	resolvconf_fn = NULL;
	return -1;
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
	octx->diagnostic(L"%d outstanding resolution%s",er,er == 1 ? "" : "s");
	if( (er = pthread_mutex_destroy(&rqueue_lock)) ){
		octx->diagnostic(L"Error destroying resolvq lock (%s)",strerror(er));
	}
	if( (er = pthread_mutex_destroy(&resolver_lock)) ){
		octx->diagnostic(L"Error destroying resolver lock (%s)",strerror(er));
	}
	free_resolvers(&resolvers6);
	free_resolvers(&resolvers);
	free(resolvconf_fn);
	resolvconf_fn = NULL;
	return er;
}
