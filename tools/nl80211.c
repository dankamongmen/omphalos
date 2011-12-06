#include <stdio.h>
#include <stdarg.h>
#include <omphalos/nl80211.h>

void diagnostic(const char *fmt,...){
	va_list va;

	va_start(va,fmt);
	vfprintf(stderr,fmt,va);
	va_end(va);
}

int main(void){
	open_nl80211();
	close_nl80211();
}
