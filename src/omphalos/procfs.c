#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <omphalos/procfs.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static procfs_state netstate = {
	.ipv4_forwarding = -1,
	.ipv6_forwarding = -1,
	.proxyarp = -1,
	.tcp_ccalg = NULL,
};

static pthread_mutex_t netlock = PTHREAD_MUTEX_INITIALIZER;

// Destructively lex a single uint from the file.
static int
lex_unsigned(FILE *fp,unsigned long *val){
	char buf[80],*e;

	if(fgets(buf,sizeof(buf),fp) == NULL){
		return -1;
	}
	if((*val = strtoul(buf,&e,0)) == ULONG_MAX){
		if(errno){
			return -1;
		}
	}
	if(*e != '\n'){
		return -1;
	}
	return 0;
}

// Destructively lex a single alphanumeric/'_'/'-' token from the file. The
// token can't be more than 80 characters because I'm laaaaaaame FIXME
static char *
lex_string(FILE *fp){
	char buf[80],*e;

	if(fgets(buf,sizeof(buf),fp) == NULL){
		return NULL;
	}
	e = buf;
	while(isalnum(*e) || *e == '-' || *e == '_'){
		++e;
	}
	if(e == buf || *e != '\n'){
		return NULL;
	}
	return strdup(buf);
}

// Return the lexed value as a { -1, 0, 1} value
static inline int
lex_binary(FILE *fp){
	unsigned long val;

	if(lex_unsigned(fp,&val)){
		return -1;
	}
	if(val == 0){
		return 0;
	}
	if(val == 1){
		return 1;
	}
	return -1;
}

static int
lex_binary_file(const omphalos_iface *octx,const char *fn){
	FILE *fp;
	int val;

	if((fp = fopen(fn,"r")) == NULL){
		octx->diagnostic(L"Couldn't open %s (%s?)",fn,strerror(errno));
		return -1;
	}
	if((val = lex_binary(fp)) < 0){
		octx->diagnostic(L"Error parsing %s",fn);
		fclose(fp);
		return -1;
	}
	if(fclose(fp)){
		octx->diagnostic(L"Error closing %s (%s?)",fn,strerror(errno));
		return -1;
	}
	return val;
}

static char *
lex_string_file(const omphalos_iface *octx,const char *fn){
	char *val;
	FILE *fp;

	if((fp = fopen(fn,"r")) == NULL){
		octx->diagnostic(L"Couldn't open %s (%s?)",fn,strerror(errno));
		return NULL;
	}
	if((val = lex_string(fp)) == NULL){
		octx->diagnostic(L"Error parsing %s",fn);
		fclose(fp);
		return NULL;
	}
	if(fclose(fp)){
		octx->diagnostic(L"Error closing %s (%s?)",fn,strerror(errno));
		free(val);
		return NULL;
	}
	return val;
}

static int
proc_ipv4_ip_forward(const omphalos_iface *octx,const char *fn){
	int ipv4f;

	if((ipv4f = lex_binary_file(octx,fn)) < 0){
		return -1;
	}
	pthread_mutex_lock(&netlock);
		netstate.ipv4_forwarding = ipv4f;
	pthread_mutex_unlock(&netlock);
	return 0;
}

static int
proc_ipv6_ip_forward(const omphalos_iface *octx,const char *fn){
	int ipv6f;

	if((ipv6f = lex_binary_file(octx,fn)) < 0){
		return -1;
	}
	pthread_mutex_lock(&netlock);
		netstate.ipv6_forwarding = ipv6f;
	pthread_mutex_unlock(&netlock);
	return 0;
}

static int
proc_proxy_arp(const omphalos_iface *octx,const char *fn){
	int parp;

	if((parp = lex_binary_file(octx,fn)) < 0){
		return -1;
	}
	pthread_mutex_lock(&netlock);
		netstate.proxyarp = parp;
	pthread_mutex_unlock(&netlock);
	return 0;
}

static int
proc_tcp_ccalg(const omphalos_iface *octx,const char *fn){
	char *ccalg;

	if((ccalg = lex_string_file(octx,fn)) == NULL){
		return -1;
	}
	pthread_mutex_lock(&netlock);
		free(netstate.tcp_ccalg);
		netstate.tcp_ccalg = ccalg;
	pthread_mutex_unlock(&netlock);
	return 0;
}

static const struct procent {
	const char *path;
	watchcbfxn fxn;
} procents[] = {
	{ .path = "sys/net/ipv4/conf/all/forwarding",	.fxn = proc_ipv4_ip_forward,	},
	{ .path = "sys/net/ipv6/conf/all/forwarding",	.fxn = proc_ipv6_ip_forward,	},
	{ .path = "sys/net/ipv4/conf/all/proxy_arp",	.fxn = proc_proxy_arp,		},
	{ .path = "sys/net/ipv4/tcp_congestion_control",.fxn = proc_tcp_ccalg,		},
	{ .path = NULL,					.fxn = NULL,			}
};

int init_procfs(const omphalos_iface *octx,const char *procroot){
	const struct procent *p;
	char path[PATH_MAX],*pp;
	int s,ps;

	ps = sizeof(path);
	if((s = snprintf(path,sizeof(path),"%s",procroot)) >= ps){
		octx->diagnostic(L"Bad procfs mountpath: %s",procroot);
		return -1;
	}
	ps -= s;
	pp = path + s;
	for(p = procents ; p->path ; ++p){
		if(snprintf(pp,ps,"%s",p->path) >= ps){
			octx->diagnostic(L"Bad path: %s%s",procroot,p->path);
			return -1;
		}else if(watch_file(octx,path,p->fxn)){
			return -1;
		}
	}
	return 0;
}

int cleanup_procfs(const omphalos_iface *octx){
	int err;

	if( (err = pthread_mutex_destroy(&netlock)) ){
		octx->diagnostic(L"Error destroying netlock (%s?)",strerror(err));
		return -1;
	}
	free(netstate.tcp_ccalg);
	return 0;
}

int get_procfs_state(procfs_state *ps){
	if(pthread_mutex_lock(&netlock)){
		return -1;
	}
	memcpy(ps,&netstate,sizeof(*ps));
	ps->tcp_ccalg = strdup(ps->tcp_ccalg);
	pthread_mutex_unlock(&netlock);
	if(ps->tcp_ccalg == NULL){
		return -1;
	}
	return 0;
}

void clean_procfs_state(procfs_state *ps){
	if(ps){
		free(ps->tcp_ccalg);
	}
}
