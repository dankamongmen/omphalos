#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/inotify.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>

static int inotify_fd = -1;

int watch_init(const omphalos_iface *octx){
	if((inotify_fd = inotify_init1(IN_CLOEXEC)) < 0){
		octx->diagnostic("Couldn't open inotify fd (%s?)",strerror(errno));
		return -1;
	}
	return inotify_fd;
}

int watch_file(const omphalos_iface *octx,const char *fn){
	if(inotify_add_watch(inotify_fd,fn,0)){
		octx->diagnostic("Couldn't register %s",fn);
		return -1;
	}
	return 0;
}

int watch_stop(const omphalos_iface *octx){
	int ret = 0;

	if(inotify_fd >= 0){
		if(close(inotify_fd) != 0){
			octx->diagnostic("Couldn't close inotify fd %d",inotify_fd);
			ret = -1;
		}
		inotify_fd = -1;
	}
	return ret;
}
