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

static const struct procent {
	const char *path;
	watchcbfxn fxn;
} procents[] = {
	{ .path = "sys/net/ipv4/conf/all/forwarding",	.fxn = proc_ipv4_ip_forward,		},
	{ .path = "sys/net/ipv6/conf/all/forwarding",	.fxn = proc_ipv6_ip_forward,		},
	{ .path = NULL,				.fxn = NULL,		}
};

int init_procfs(const omphalos_iface *octx,const char *procroot){
	const struct procent *p;
	char path[PATH_MAX],*pp;
	int s,ps;

	ps = sizeof(path);
	if((s = snprintf(path,sizeof(path),"%s/",procroot)) >= ps){
		octx->diagnostic(L"Bad procfs mountpath: %s",procroot);
		return -1;
	}
	ps -= s;
	pp = path + s;
	for(p = procents ; p->path ; ++p){
		if(snprintf(pp,ps,"%s",p->path) >= ps){
			octx->diagnostic(L"Bad path: %s/%s",procroot,p->path);
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
	return 0;
}

int get_procfs_state(procfs_state *ps){
	pthread_mutex_lock(&netlock);
	memcpy(ps,&netstate,sizeof(*ps));
	pthread_mutex_unlock(&netlock);
	return 0;
}
