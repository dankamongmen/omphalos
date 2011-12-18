#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <omphalos/nl80211.h>
#include <omphalos/omphalos.h>

static void
diag(const char *fmt,va_list va){
	fprintf(stderr,fmt,va);
	fputc('\n',stderr);
	va_end(va);
}

int main(void){
	omphalos_ctx ctx = {
		.iface = {
			.vdiagnostic = diag,
		},
	};
	int ret;

	pthread_key_create(&omphalos_ctx_key,NULL);
	pthread_setspecific(omphalos_ctx_key,&ctx);
	if(open_nl80211()){
		fprintf(stderr,"Failure opening cfg80211\n");
		ret = -1;
	}else{
		fprintf(stdout,"Successfully opened cfg80211\n");
		ret |= close_nl80211();
	}
	exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
}
