#ifndef OMPHALOS_UI_NCURSES_ENVDISP
#define OMPHALOS_UI_NCURSES_ENVDISP

#ifdef __cplusplus
extern "C" {
#endif

#include <ncursesw/ncurses.h>

struct panel_state;

int display_env_locked(WINDOW *,struct panel_state *);

#ifdef __cplusplus
}
#endif

#endif
