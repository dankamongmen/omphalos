#include <assert.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/util.h>
#include <omphalos/omphalos.h>

#define LOG_RINGBUF_SIZE 1024

static unsigned rblast;
static char *logs[LOG_RINGBUF_SIZE];
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
		if(logs[rblast]){
			free(logs[rblast]);
		}
		logs[rblast] = b;
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

char *get_logs(unsigned n,int sep){
	unsigned rb,left;
	size_t len = 0;
	char *l = NULL;

	left = n;
	Pthread_mutex_lock(&loglock);
	rb = rblast;
	while(logs[rb]){
		size_t nlen;
		char *tmp;

		if(n && !left--){
			break; // got all requested
		}
		// FIXME concatenate
		nlen = len + strlen(logs[rb]) + 1;
		if((tmp = realloc(l,nlen + 1)) == NULL){
			free(l);
			Pthread_mutex_unlock(&loglock);
			diagnostic("%s couldn't allocate %zu bytes",__func__,nlen);
			return NULL;
		}
		l = tmp;
		strcpy(l + len,logs[rb]);
		len = nlen;
		l[len - 1] = sep;
		if(rb-- == 0){
			rb = sizeof(logs) / sizeof(*logs) - 1;
		}
	}
	Pthread_mutex_unlock(&loglock);
	if(l){
		l[len] = '\0'; // safe; we allocated len + 1
	}
	return l;
}
