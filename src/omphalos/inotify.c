#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <omphalos/diag.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static int inotify_fd = -1;

// Callback table, indexed by watch descriptors returned by inotify_add_watch()
static int watchcbmax;
static struct watch {
	watchcbfxn fxn;
	char path[PATH_MAX];
} *fxns;

static pthread_mutex_t ilock = PTHREAD_MUTEX_INITIALIZER;

static int
watch_init_locked(void){
	if(inotify_fd < 0){
		if((inotify_fd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC)) < 0){
			diagnostic("Couldn't open inotify fd (%s?)",strerror(errno));
			return -1;
		}
	}
	return inotify_fd;
}

int watch_init(void){
	int ret;

	// FIXME shouldn't be calling diagnostics etc within critical section!
	pthread_mutex_lock(&ilock);
	ret = watch_init_locked();
	pthread_mutex_unlock(&ilock);
	return ret;
}

// Failures are allowed during runtime reprocessing, but not during init -- if
// the watchcbfxn returns non-zero on init, no watch is registered.
int watch_file(const char *fn,watchcbfxn fxn){
	int ret = -1;

	if(fxn(fn)){
		return -1;
	}
	// FIXME shouldn't be calling diagnostics etc within critical section!
	pthread_mutex_lock(&ilock);
	if(watch_init_locked() >= 0){
		int wd;

		if((wd = inotify_add_watch(inotify_fd,fn,IN_DELETE_SELF|IN_MODIFY)) < 0){
			diagnostic("Couldn't register %s (%s?)",fn,strerror(errno));
		}else{
			if(wd >= watchcbmax){
				void *tmp;

				if((tmp = realloc(fxns,(wd + 1) * sizeof(*fxns))) == NULL){
					diagnostic("Couldn't register %d (%s?)",wd,strerror(errno));
					inotify_rm_watch(inotify_fd,wd);
				}else{
					fxns = tmp;
					watchcbmax = wd + 1;
				}
			}
			ret = 0;
			fxns[wd].fxn = fxn;
			strcpy(fxns[wd].path,fn);
		}
	}
	pthread_mutex_unlock(&ilock);
	return ret;
}

int handle_watch_event(int fd){
	struct inotify_event event;
	ssize_t r;

	while((r = read(fd,&event,sizeof(event))) == sizeof(event)){
		if(event.wd >= watchcbmax){
			diagnostic("%08x event %d too large on %d",
					event.mask,event.wd,event.wd);
		}
		if(fxns[event.wd].fxn){
			fxns[event.wd].fxn(fxns[event.wd].path);
		}else{
			diagnostic("No handler for %d (%08x) on %d",
						event.wd,event.mask,event.wd);
		}
	}
	if(r < 0){
		if(errno == EAGAIN){
			return 0;
		}
		diagnostic("Error reading inotify socket %d (%s?)",fd,strerror(errno));
		// FIXME do what?
	}else{
		diagnostic("Short read on inotify socket %d (%zd)",fd,r);
		// FIXME do what?
	}
	return -1;
}

int watch_stop(void){
	int ret = 0,fd;

	pthread_mutex_lock(&ilock);
	fd = inotify_fd;
	inotify_fd = -1;
	pthread_mutex_unlock(&ilock);
	if(fd >= 0){
		if(close(fd) != 0){
			diagnostic("Couldn't close inotify fd %d",fd);
			ret = -1;
		}
	}
	free(fxns);
	fxns = NULL;
	return ret;
}
