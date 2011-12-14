#include <assert.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/omphalos.h>

#define LOG_RINGBUF_SIZE 1024

static unsigned rbstart;
static char *logs[LOG_RINGBUF_SIZE];
static pthread_mutex_t loglock = PTHREAD_MUTEX_INITIALIZER;


void diagnostic(const char *fmt,...){
	const omphalos_ctx *octx = get_octx();
	va_list va;

	va_start(va,fmt);
	octx->iface.vdiagnostic(fmt,va);
	va_end(va);
}

char *get_logs(unsigned n,int sep){
	unsigned rb,left;
	size_t len = 1;
	char *l = NULL;

	left = n;
	Pthread_mutex_lock(&loglock);
	rb = rbstart;
	while(logs[rb]){
		size_t nlen;
		char *tmp;

		if(n && !left--){
			break; // got all requested
		}
		// FIXME concatenate
		nlen = len + 1;
		if((tmp = realloc(l,nlen)) == NULL){
			free(l);
			Pthread_mutex_unlock(&loglock);
			diagnostic("%s couldn't allocate %zu bytes",__func__,nlen);
			return NULL;
		}
		l = tmp;
		len = nlen;
		l[len - 2] = sep;
		if(++rb == sizeof(logs) / sizeof(*logs)){
			rb = 0;
		}
		if(rb == rbstart){
			break;
		}
	}
	Pthread_mutex_unlock(&loglock);
	if(l){
		l[len - 1] = '\0';
	}
	return l;
}
