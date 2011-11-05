#include <assert.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>

void diagnostic(const wchar_t *fmt,...){
	const omphalos_ctx *octx;
	va_list va;

	octx = pthread_getspecific(omphalos_ctx_key);
	assert(octx);
	va_start(va,fmt);
	vfwprintf(stderr,fmt,va);
	va_end(va);
}
