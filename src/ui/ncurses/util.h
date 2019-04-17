#ifndef OMPHALOS_UI_NCURSES_UTIL
#define OMPHALOS_UI_NCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/color.h>

#define PAD_LINES 3
#define START_COL 1
#define PAD_COLS(cols) ((cols) - START_COL)
#define START_LINE 1

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define U32FMT "%-10ju"
#define PREFIXSTRLEN 7	// Does not include a '\0' (xxx.xxU)
#define PREFIXFMT "%7s"

#define SUBDISPLAY_ATTR (COLOR_PAIR(SUBDISPLAY_COLOR) | A_BOLD)

#define COMB_UNDER '\u0332'

// FIXME eliminate all callers!
static inline void
unimplemented(WINDOW *w){
	wstatus_locked(w,"Sorry bro; that ain't implemented yet!");
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
int bevel_top(WINDOW *);
int bevel_bottom(WINDOW *);

const char *genprefix(uintmax_t,unsigned,char *,size_t,int,unsigned,int);

static inline const char *
prefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1000,'\0');
}

static inline const char *
bprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1024,'i');
}

#ifdef __cplusplus
}
#endif

#endif
