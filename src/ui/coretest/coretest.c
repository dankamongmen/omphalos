#include <stdio.h>
#include <wchar.h>
#include <errno.h>
#include <iwlib.h>
#include <locale.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <net/if.h>
#include <langinfo.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <wireless.h>
#include <omphalos/diag.h>
#include <omphalos/pcap.h>
#include <omphalos/service.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

int main(int argc,char * const *argv){
	const char *codeset;
	omphalos_ctx pctx;

	assert(fwide(stdout,-1) < 0);
	assert(fwide(stderr,-1) < 0);
	if(setlocale(LC_ALL,"") == NULL || ((codeset = nl_langinfo(CODESET)) == NULL)){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if(strcmp(codeset,"UTF-8")){
		fprintf(stderr,"Only UTF-8 is supported; got %s\n",codeset);
		// return EXIT_FAILURE;
	}
	if(omphalos_setup(argc,argv,&pctx)){
		return EXIT_FAILURE;
	}
	if(omphalos_init(&pctx)){
		return EXIT_FAILURE;
	}
	omphalos_cleanup(&pctx);
	return EXIT_SUCCESS;
}
