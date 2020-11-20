#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <omphalos/diag.h>
#include <omphalos/procfs.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static procfs_state netstate = {
	.ipv4_forwarding = -1,
	.ipv6_forwarding = -1,
	.proxyarp = -1,
	.rp_filter = -1,
	.tcp_ccalg = NULL,
	.tcp_frto = -1,
	.tcp_sack = -1,
	.tcp_dsack = -1,
	.tcp_fack = -1,
};

static pthread_mutex_t netlock = PTHREAD_MUTEX_INITIALIZER;

static inline void
lock_net(void){
	pthread_mutex_lock(&netlock);
}

static inline void
unlock_net(void){
	const omphalos_ctx *ctx;

	pthread_mutex_unlock(&netlock);
	if( (ctx = get_octx()) ){
		if(ctx->iface.network_event){
			ctx->iface.network_event();
		}
	}
}

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
	*e = '\0';
	return strdup(buf);
}

// Return the lexed value as a { -1, 0, 1, ... n} value
static inline int
lex_state(FILE *fp,unsigned n){
	unsigned long val;

	if(lex_unsigned(fp,&val)){
		return -1;
	}
	if(val <= n){
		return val;
	}
	return -1;
}

static int
lex_unsigned_file(const char *fn, unsigned long n){
	FILE *fp;
	int val;

	if(n > INT_MAX){
		return -1;
	}
	if((fp = fopen(fn, "r")) == NULL){
		diagnostic("Couldn't open %s (%s?)", fn, strerror(errno));
		return -1;
	}
	if((val = lex_state(fp, n)) < 0){
		diagnostic("Error parsing %s", fn);
		fclose(fp);
		return -1;
	}
	if(fclose(fp)){
		diagnostic("Error closing %s (%s?)", fn, strerror(errno));
		return -1;
	}
	return val;
}

static char *
lex_string_file(const char *fn){
	char *val;
	FILE *fp;

	if((fp = fopen(fn, "r")) == NULL){
		diagnostic("Couldn't open %s (%s?)", fn, strerror(errno));
		return NULL;
	}
	if((val = lex_string(fp)) == NULL){
		diagnostic("Error parsing %s", fn);
		fclose(fp);
		return NULL;
	}
	if(fclose(fp)){
		diagnostic("Error closing %s (%s?)", fn, strerror(errno));
		free(val);
		return NULL;
	}
	return val;
}

static int
proc_ipv4_ip_forward(const char *fn){
	int ipv4f;

	if((ipv4f = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.ipv4_forwarding = ipv4f;
	unlock_net();
	return 0;
}

static int
proc_ipv6_ip_forward(const char *fn){
	int ipv6f;

	if((ipv6f = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.ipv6_forwarding = ipv6f;
	unlock_net();
	return 0;
}

static int
proc_proxy_arp(const char *fn){
	int parp;

	if((parp = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.proxyarp = parp;
	unlock_net();
	return 0;
}

static int
proc_rp_filter(const char *fn){
	int rp;

	if((rp = lex_unsigned_file(fn,2)) < 0){
		return -1;
	}
	lock_net();
		netstate.rp_filter = rp;
	unlock_net();
	return 0;
}

// FIXME this is a legacy setting, no longer used, remove it
static int
proc_tcp_fack(const char *fn){
	int fack;

	if((fack = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.tcp_fack = fack;
	unlock_net();
	return 0;
}

static int
proc_tcp_frto(const char *fn){
	int frto;

	if((frto = lex_unsigned_file(fn,2)) < 0){
		return -1;
	}
	lock_net();
		netstate.tcp_frto = frto;
	unlock_net();
	return 0;
}

static int
proc_tcp_sack(const char *fn){
	int sack;

	if((sack = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.tcp_sack = sack;
	unlock_net();
	return 0;
}

static int
proc_tcp_dsack(const char *fn){
	int dsack;

	if((dsack = lex_unsigned_file(fn,1)) < 0){
		return -1;
	}
	lock_net();
		netstate.tcp_dsack = dsack;
	unlock_net();
	return 0;
}

static int
proc_tcp_ccalg(const char *fn){
	char *ccalg;

	if((ccalg = lex_string_file(fn)) == NULL){
		return -1;
	}
	lock_net();
		free(netstate.tcp_ccalg);
		netstate.tcp_ccalg = ccalg;
	unlock_net();
	return 0;
}

static const struct procent {
	const char *path;
	watchcbfxn fxn;
} procents[] = {
	// List synonyms together, for now...?
	{ .path = "sys/net/ipv4/conf/all/forwarding",	.fxn = proc_ipv4_ip_forward,	},
	{ .path = "sys/net/ipv4/ip_forward",		.fxn = proc_ipv4_ip_forward,	},
	{ .path = "sys/net/ipv6/conf/all/forwarding",	.fxn = proc_ipv6_ip_forward,	},
	{ .path = "sys/net/ipv4/conf/all/proxy_arp",	.fxn = proc_proxy_arp,		},
	{ .path = "sys/net/ipv4/conf/all/rp_filter",	.fxn = proc_rp_filter,		},
	{ .path = "sys/net/ipv4/tcp_congestion_control",.fxn = proc_tcp_ccalg,		},
	{ .path = "sys/net/ipv4/tcp_fack",		.fxn = proc_tcp_fack,		},
	{ .path = "sys/net/ipv4/tcp_frto",		.fxn = proc_tcp_frto,		},
	{ .path = "sys/net/ipv4/tcp_sack",		.fxn = proc_tcp_sack,		},
	{ .path = "sys/net/ipv4/tcp_dsack",		.fxn = proc_tcp_dsack,		},
	{ .path = NULL,					.fxn = NULL,			}
};

int init_procfs(const char *procroot){
	const struct procent *p;
	char path[PATH_MAX],*pp;
	int s,ps;

	ps = sizeof(path);
	if((s = snprintf(path,sizeof(path),"%s",procroot)) >= ps){
		diagnostic("Bad procfs mountpath: %s",procroot);
		return -1;
	}
	ps -= s;
	pp = path + s;
	for(p = procents ; p->path ; ++p){
		if(snprintf(pp,ps,"%s",p->path) >= ps){
			diagnostic("Bad path: %s%s",procroot,p->path);
			return -1;
		}else if(watch_file(path,p->fxn)){
			return -1;
		}
	}
	return 0;
}

int cleanup_procfs(void){
	int err;

	if( (err = pthread_mutex_destroy(&netlock)) ){
		diagnostic("Error destroying netlock (%s?)",strerror(err));
		return -1;
	}
	free(netstate.tcp_ccalg);
	return 0;
}

int get_procfs_state(procfs_state *ps){
	lock_net();
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
