#include <limits.h>
#include <omphalos/procfs.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static int
proc_ipv4_ip_forward(const omphalos_iface *octx){
	octx->diagnostic(L"hrmmm...");
	return -1;
}

static const struct procent {
	const char *path;
	watchcbfxn fxn;
} procents[] = {
	{ .path = "net/ipv4/ip_forward",	.fxn = proc_ipv4_ip_forward,		},
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
