#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <omphalos/dns.h>
#include <omphalos/mdns.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <asm/byteorder.h>
#include <omphalos/resolv.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/inotify.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

typedef struct resolver {
	union {
		struct in_addr ip4;
		struct in6_addr ip6;
	} addr;
	struct resolver *next;
} resolver;

static resolver *resolvers,*resolvers6;
static pthread_mutex_t resolver_lock = PTHREAD_MUTEX_INITIALIZER;

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

int queue_for_naming(struct interface *i,struct l3host *l3,dnstxfxn dnsfxn,
			const char *revstr,int fam,const void *lookup){
	int ret = 0;

	if(get_l3nlevel(l3) < NAMING_LEVEL_NXDOMAIN){
		uint128_t addr;
		void *ad;

		if(pthread_mutex_lock(&resolver_lock)){
			return -1;
		}
		// FIXME round-robin or even use the simple resolv.conf algorithm
		// We don't call dnsfxn() while holding the resolvers lock,
		// because it can lead to deadlock (interface A resolves using
		// interface B, acquiring resolver lock. interface B takes its
		// own lock, and wants to resolve, blocking on resolver lock.
		// interface A needs get_tx_frame() and routing lookups on B,
		// blocking on B's lock ---> deadlock).
		if(resolvers){
			ad = &resolvers->addr.ip4;
			memcpy(&addr,ad,sizeof(resolvers->addr.ip4));
		}else{
			ad = NULL;
		}
		pthread_mutex_unlock(&resolver_lock);
		if(ad){
			ret = dnsfxn(AF_INET,&addr,revstr);
		}
		if(pthread_mutex_lock(&resolver_lock)){
			return -1;
		}
		if(resolvers6){
			ad = &resolvers6->addr.ip6;
			memcpy(&addr,ad,sizeof(resolvers6->addr.ip6));
		}else{
			ad = NULL;
		}
		pthread_mutex_unlock(&resolver_lock);
		if(ad){
			ret = dnsfxn(AF_INET6,&addr,revstr);
		}
	}
	ret |= tx_mdns_ptr(i,revstr,fam,lookup);
	return ret;
}

void offer_nameserver(int nsfam,const void *nameserver){
	const omphalos_ctx *ctx = get_octx();
	const omphalos_iface *octx = &ctx->iface;
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
	if(octx->network_event){
		octx->network_event();
	}
}

int offer_resolution(int fam,const void *addr,const char *name,namelevel nlevel,
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
	r = offer_wresolution(fam,addr,wname,nlevel,nsfam,nameserver);
	free(wname);
	return r;
}

int offer_wresolution(int fam,const void *addr,const wchar_t *name,namelevel nlevel,
				int nsfam __attribute__ ((unused)),
				const void *nameserver __attribute__ ((unused))){
	struct interface *i;
	struct l3host *l3;
	struct l2host *l2;

	// FIXME don't call until we filter offers better
	// offer_nameserver(nsfam,nameserver);
	if((l3 = lookup_global_l3host(fam,addr)) == NULL){
		return 0;
	}
	// FIXME needs to lock the interface to touch l3 objs
	l2 = l3_getlastl2(l3);
	i = l2_getiface(l2);
	wname_l3host_absolute(i,l2,l3,name,nlevel);
	/*{
		char abuf[INET6_ADDRSTRLEN],rbuf[INET6_ADDRSTRLEN];

		inet_ntop(fam,addr,abuf,sizeof(abuf));
		inet_ntop(nsfam,nameserver,rbuf,sizeof(rbuf));
		diagnostic("Resolved %s @%s as %ls",abuf,rbuf,name);
	}*/
	return 0;
}

static int
parse_resolv_conf(const char *fn){
	struct timeval t0,t1,t2;
	resolver *revs = NULL;
	unsigned count = 0;
	int l,ret = -1;
	char *line;
	FILE *fp;
	char *b;

	gettimeofday(&t0,NULL);
	if((fp = fopen(fn,"r")) == NULL){
		diagnostic("Couldn't open %s",fn);
		return -1;
	}
	b = NULL;
	l = 0;
	errno = 0;
	while( (line = fgetl(&b,&l,fp)) ){
		struct in6_addr ina6;
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
		if(*line == '['){
			if(inet_pton(AF_INET6,line,&ina6) != 1){
				continue;
			}
			// check terminating ']'? FIXME
			if((r = create_resolver(&ina6,sizeof(ina6))) == NULL){
				break; // FIXME
			}
		}else{
			if(inet_pton(AF_INET,line,&ina) != 1){
				continue;
			}
			// check that nothing follows? FIXME
			if((r = create_resolver(&ina,sizeof(ina))) == NULL){
				break; // FIXME
			}
		}
		r->next = revs;
		revs = r;
		++count;
		// FIXME
#undef NSTOKEN
	}
	free(b);
	fclose(fp);
	if(errno){
		free_resolvers(&revs);
	}else{
		const omphalos_ctx *ctx = get_octx();
		const omphalos_iface *octx = &ctx->iface;
		resolver *r;

		pthread_mutex_lock(&resolver_lock);
		r = resolvers;
		resolvers = revs;
		pthread_mutex_unlock(&resolver_lock);
		if(octx->network_event){
			octx->network_event();
		}
		free_resolvers(&r);
		gettimeofday(&t1,NULL);
		timersub(&t1,&t0,&t2);
		diagnostic("Reloaded %u resolver%s from %s in %lu.%07lus",count,
				count == 1 ? "" : "s",fn,t2.tv_sec,t2.tv_usec);
		ret = 0;
	}
	return ret;
}

int init_naming(const char *resolvconf){
	if(watch_file(resolvconf,parse_resolv_conf)){
		return -1;
	}
	return 0;
}

int cleanup_naming(void){
	int er;

	er = 0;
	pthread_mutex_lock(&resolver_lock);
	free_resolvers(&resolvers6);
	free_resolvers(&resolvers);
	pthread_mutex_unlock(&resolver_lock);
	if( (er = pthread_mutex_destroy(&resolver_lock)) ){
		diagnostic("Error destroying resolver lock (%s)",strerror(er));
	}
	return er;
}

char *stringize_resolvers(void){
	char buf[INET6_ADDRSTRLEN];
	char *ret = NULL,*tmp;
	const resolver *r;
	size_t s = 0;

	pthread_mutex_lock(&resolver_lock);
	for(r = resolvers ; r ; r = r->next){
		assert(inet_ntop(AF_INET,&r->addr.ip4,buf,sizeof(buf)));
		if((tmp = realloc(ret,s + strlen(buf) + 1)) == NULL){
			goto err;
		}
		ret = tmp;
		if(s){
			ret[s - 1] = ',';
		}
		strcpy(ret + s,buf);
		s += strlen(buf) + 1;
	}
	for(r = resolvers6 ; r ; r = r->next){
		assert(inet_ntop(AF_INET6,&r->addr.ip6,buf,sizeof(buf)));
		if((tmp = realloc(ret,s + strlen(buf) + 1)) == NULL){
			goto err;
		}
		ret = tmp;
		if(s){
			ret[s - 1] = ',';
		}
		strcpy(ret + s,buf);
		s += strlen(buf) + 1;
	}
	pthread_mutex_unlock(&resolver_lock);
	return ret;

err:
	free(ret);
	return NULL;
}
