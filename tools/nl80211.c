#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <omphalos/nl80211.h>
#include <omphalos/omphalos.h>

int main(void){
	int ret;

	pthread_setspecific(omphalos_ctx_key,NULL);
	if(open_nl80211()){
		fprintf(stderr,"Failure opening nl80211\n");
		ret = -1;
	}else{
		ret |= close_nl80211();
	}
	exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
}
