#ifndef OMPHALOS_UI_NOTCURSES_UTIL
#define OMPHALOS_UI_NOTCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <ui/notcurses/core.h>
#include <notcurses/notcurses.h>

#define PAD_LINES 3
#define START_COL 1
#define PAD_COLS(cols) ((cols) - START_COL)
#define START_LINE 1

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define U32FMT "%-10ju"

#define SUBDISPLAY_ATTR (COLOR_PAIR(SUBDISPLAY_COLOR) | A_BOLD)

#define COMB_UNDER '\u0332'

// FIXME eliminate all callers!
static inline void
unimplemented(struct ncplane *w){
	wstatus_locked(w,"Sorry bro; that ain't implemented yet!");
}

static inline int
finish_screen_update(void){
  if(notcurses_render(NC)){
    return -1;
  }
	return 0;
}

static inline int
screen_update(void){
	return finish_screen_update();
}

int bevel(struct ncplane *);
int bevel_top(struct ncplane *);
int bevel_bottom(struct ncplane *);

#ifdef __cplusplus
}
#endif

#endif
