#include <assert.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>

void diagnostic(const char *fmt,...){
	const omphalos_ctx *octx = get_octx();
	va_list va;

	va_start(va,fmt);
	octx->iface.vdiagnostic(fmt,va);
	va_end(va);
}
