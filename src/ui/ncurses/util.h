#ifndef OMPHALOS_UI_NCURSES_UTIL
#define OMPHALOS_UI_NCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <ncursesw/ncurses.h>

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define PREFIXSTRLEN 7	// Does not include a '\0' (xxx.xxU)
#define PREFIXFMT "%7s"

int bevel(WINDOW *,int);

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
	HEADER_COLOR,
	FOOTER_COLOR,
	DBORDER_COLOR,			// down interfaces
	DHEADING_COLOR,
	UBORDER_COLOR,			// up interfaces
	UHEADING_COLOR,
	PBORDER_COLOR,			// popups
	PHEADING_COLOR,
	BULKTEXT_COLOR,			// bulk text (help, details)
	IFACE_COLOR,			// interface summary text
	LCAST_COLOR,			// local addresses
	UCAST_COLOR,			// unicast addresses
	MCAST_COLOR,			// multicast addresses
	BCAST_COLOR,			// broadcast addresses
	ROUTER_COLOR,			// routing l3 addresses / access points
	MAX_OMPHALOS_COLOR
};

int setup_extended_colors(void);

// FIXME on 8-color terminals (linux, xterm), we can't define more than
// COLOR_WHITE + 1 colors at once. we'll need use A_BOLD there, which is not a
// problem since we can't fade on those terminals anyway (can_color_change()
// returns FALSE).
enum {
	COLOR_BOLDCYAN  = COLOR_WHITE + 1,
	COLOR_PURPLE,
};

void fade(unsigned);

#ifdef __cplusplus
}
#endif

#endif
