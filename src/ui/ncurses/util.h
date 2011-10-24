#ifndef OMPHALOS_UI_NCURSES_UTIL
#define OMPHALOS_UI_NCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <ncursesw/panel.h>
#include <ui/ncurses/core.h>
#include <ncursesw/ncurses.h>

#define PAD_LINES 3
#define START_COL 1
#define PAD_COLS(cols) ((cols) - START_COL * 2)
#define START_LINE 1

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define PREFIXSTRLEN 7	// Does not include a '\0' (xxx.xxU)
#define PREFIXFMT "%7s"

// FIXME eliminate all callers!
static inline void
unimplemented(WINDOW *w){
	wstatus_locked(w,L"Sorry bro; that ain't implemented yet!");
}

static inline int
start_screen_update(void){
	int ret = OK;

	update_panels();
	return ret;
}

static inline int
finish_screen_update(void){
	// FIXME we definitely don't need wrefresh() in its entirety?
	if(doupdate() == ERR){
		return ERR;
	}
	return OK;
}

static inline int
screen_update(void){
	int ret;

	assert((ret = start_screen_update()) == 0);
	assert((ret |= finish_screen_update()) == 0);
	return ret;
}

int bevel(WINDOW *);
int bevel_notop(WINDOW *);
int bevel_nobottom(WINDOW *);

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
	COLOR_BLUE_75 = 16,
	COLOR_CYAN_75,
};

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
	LCAST_L3_COLOR,			// local l3
	UCAST_L3_COLOR,			// unicast l3
	MCAST_L3_COLOR,			// multicast l3
	BCAST_L3_COLOR,			// broadcast l3
	MAX_OMPHALOS_COLOR
};

int setup_extended_colors(void);

void fade(unsigned);

#ifdef __cplusplus
}
#endif

#endif
