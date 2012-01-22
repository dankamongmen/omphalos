#include <assert.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/omphalos.h>

static unsigned rblast;
static logent logs[MAXIMUM_LOG_ENTRIES];
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;

static void
add_log(const char *fmt,va_list vac){
	va_list vacc;
	char *b;
	int len;

	va_copy(vacc,vac);
	Pthread_mutex_lock(&loglock);
	if(++rblast == sizeof(logs) / sizeof(*logs)){
		rblast = 0;
	}
	// FIXME reuse the entry!
	len = vsnprintf(NULL,0,fmt,vac);
	if( (b = malloc(len + 1)) ){
		logs[rblast].when = time(NULL);
		if(logs[rblast].msg){
			free(logs[rblast].msg);
		}
		logs[rblast].msg = b;
		vsnprintf(b,len + 1,fmt,vacc);
	}
	Pthread_mutex_unlock(&loglock);
	va_end(vacc);
}

void diagnostic(const char *fmt,...){
	const omphalos_ctx *octx = get_octx();
	va_list va,vac;

	va_start(va,fmt);
	va_copy(vac,va);
	add_log(fmt,vac);
	va_end(vac);
	octx->iface.vdiagnostic(fmt,va);
	va_end(va);
}

int get_logs(unsigned n,logent *cplogs){
	unsigned idx = 0;
	unsigned rb;

	if(n == 0 || n > MAXIMUM_LOG_ENTRIES){
		return -1;
	}
	Pthread_mutex_lock(&loglock);
	rb = rblast;
	while(logs[rb].msg){
		if((cplogs[idx].msg = strdup(logs[rb].msg)) == NULL){
			while(idx){
				free(cplogs[--idx].msg);
			}
			return -1;
		}
		cplogs[idx].when = logs[rb].when;
		if(rb-- == 0){
			rb = sizeof(logs) / sizeof(*logs) - 1;
		}
		if(++idx == n){
			break; // got all requested
		}
	}
	Pthread_mutex_unlock(&loglock);
	if(idx < n){
		cplogs[idx].msg = NULL;
	}
	return 0;
}
