#include <errno.h>
#include <signal.h>
#include <string.h>
#include <omphalos/diag.h>

int restore_sigmask(void){
	sigset_t csigs;

	if(sigemptyset(&csigs) || sigaddset(&csigs,SIGINT)){
		diagnostic("Couldn't prepare sigset (%s?)",strerror(errno));
		return -1;
	}
	if(pthread_sigmask(SIG_BLOCK,&csigs,NULL)){
		diagnostic("Couldn't mask signals (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

int restore_sighandler(void){
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_RESTART,
	};

	if(sigaction(SIGINT,&sa,NULL)){
		diagnostic("Couldn't restore sighandler (%s?)",strerror(errno));
		return -1;
	}
	return 0;
}

int setup_sighandler(void (*handler)(int)){
	struct sigaction sa = {
		.sa_handler = handler,
		// SA_RESTART doesn't apply to all functions; most of the time,
		// we're sitting in poll(), which is *not* restarted...
		.sa_flags = SA_ONSTACK | SA_RESTART,
	};
	sigset_t csigs;

	if(sigemptyset(&csigs) || sigaddset(&csigs,SIGINT)){
		diagnostic("Couldn't prepare sigset (%s?)",strerror(errno));
		return -1;
	}
	if(sigaction(SIGINT,&sa,NULL)){
		diagnostic("Couldn't install sighandler (%s?)",strerror(errno));
		return -1;
	}
	if(pthread_sigmask(SIG_UNBLOCK,&csigs,NULL)){
		diagnostic("Couldn't unmask signals (%s?)",strerror(errno));
		restore_sighandler();
		return -1;
	}
	return 0;
}
