#ifndef OMPHALOS_UI_NCURSES_UTIL
#define OMPHALOS_UI_NCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <ncursesw/ncurses.h>

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"

int bevel(WINDOW *);

char *genprefix(uintmax_t,unsigned,char *,size_t,int,unsigned,int);

static inline char *
prefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1000,'\0');
}

static inline char *
bprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1024,'i');
}

enum {
	BORDER_COLOR = 1,		// main window
	HEADING_COLOR,
	DBORDER_COLOR,			// down interfaces
	DHEADING_COLOR,
	UBORDER_COLOR,			// up interfaces
	UHEADING_COLOR,
	PBORDER_COLOR,			// popups
	PHEADING_COLOR,
	BULKTEXT_COLOR,			// bulk text (help, details)
	IFACE_COLOR,			// interface summary text
	MCAST_COLOR,			// multicast addresses
	BCAST_COLOR,			// broadcast addresses
};

#ifdef __cplusplus
}
#endif

#endif
