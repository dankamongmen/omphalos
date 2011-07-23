#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static int inotify_fd = -1;
static pthread_mutex_t ilock = PTHREAD_MUTEX_INITIALIZER;

static int
watch_init_locked(const omphalos_iface *octx){
	if(inotify_fd < 0){
		if((inotify_fd = inotify_init1(IN_NONBLOCK|IN_CLOEXEC)) < 0){
			octx->diagnostic("Couldn't open inotify fd (%s?)",strerror(errno));
			return -1;
		}
	}
	return inotify_fd;
}

int watch_init(const omphalos_iface *octx){
	int ret;

	// FIXME shouldn't be calling diagnostics etc within critical section!
	pthread_mutex_lock(&ilock);
	ret = watch_init_locked(octx);
	pthread_mutex_unlock(&ilock);
	return ret;
}

int watch_file(const omphalos_iface *octx,const char *fn){
	int ret = -1;

	// FIXME shouldn't be calling diagnostics etc within critical section!
	pthread_mutex_lock(&ilock);
	if(watch_init_locked(octx) >= 0){
		int wd;

		if((wd = inotify_add_watch(inotify_fd,fn,IN_DELETE_SELF|IN_MODIFY)) < 0){
			octx->diagnostic("Couldn't register %s (%s?)",fn,strerror(errno));
		}else{
			ret = 0;
		}
	}
	pthread_mutex_unlock(&ilock);
	return ret;
}

int handle_watch_event(const omphalos_iface *octx,int fd){
	struct inotify_event event;
	ssize_t r;

	while((r = read(fd,&event,sizeof(event))) == sizeof(event)){
		octx->diagnostic("Event %x on %d",event.mask,event.wd);
		// FIXME handle event
	}
	if(r < 0){
		if(errno == EAGAIN){
			return 0;
		}
		octx->diagnostic("Error reading inotify socket %d (%s?)",fd,strerror(errno));
		// FIXME do what?
	}else{
		octx->diagnostic("Short read on inotify socket %d (%zd)",fd,r);
		// FIXME do what?
	}
	return -1;
}

int watch_stop(const omphalos_iface *octx){
	int ret = 0,fd;

	pthread_mutex_lock(&ilock);
	fd = inotify_fd;
	inotify_fd = -1;
	pthread_mutex_unlock(&ilock);
	if(fd >= 0){
		if(close(fd) != 0){
			octx->diagnostic("Couldn't close inotify fd %d",fd);
			ret = -1;
		}
	}
	return ret;
}
