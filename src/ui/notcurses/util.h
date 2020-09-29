#ifndef OMPHALOS_UI_NOTCURSES_UTIL
#define OMPHALOS_UI_NOTCURSES_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <ui/notcurses/core.h>

#define PAD_LINES 3
#define START_COL 1
#define PAD_COLS(cols) ((cols) - START_COL)
#define START_LINE 1

#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define U32FMT "%-10ju"

static inline int
set_subdisplay_attr(struct ncplane* n){
  int ret = ncplane_set_fg_rgb(n, 0xd0d0d0);
  ret |= ncplane_set_bg_rgb(n, 0x0);
  ncplane_set_styles(n, NCSTYLE_BOLD);
  return ret;
}

#define COMB_UNDER '\u0332'

// FIXME eliminate all callers!
static inline void
unimplemented(struct ncplane *n){
	wstatus_locked(n, "Sorry bro; that ain't implemented yet!");
}

int screen_update(void);

int bevel(struct ncplane *);

#ifdef __cplusplus
}
#endif

#endif
