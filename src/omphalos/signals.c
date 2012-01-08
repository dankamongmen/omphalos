#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <omphalos/diag.h>

static sigset_t savedsigs;

// If we add any other signals to this list, be sure to update the signal
// unmasking that goes on in the handling thread!
int mask_cancel_sigs(void){
	sigset_t cancelsigs;

	if(sigemptyset(&cancelsigs) || sigaddset(&cancelsigs,SIGINT)){
		fprintf(stderr,"Couldn't prep signals (%s?)\n",strerror(errno));
		return -1;
	}
	if(sigprocmask(SIG_BLOCK,&cancelsigs,&savedsigs)){
		fprintf(stderr,"Couldn't mask signals (%s?)\n",strerror(errno));
		return -1;
	}
	return 0;
}

int restore_sigmask(void){
	if(pthread_sigmask(SIG_BLOCK,&savedsigs,NULL)){
		diagnostic("Couldn't restore signal mask (%s?)",strerror(errno));
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
