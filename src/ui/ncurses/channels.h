#ifndef OMPHALOS_UI_NCURSES_CHANNELS
#define OMPHALOS_UI_NCURSES_CHANNELS

#ifdef __cplusplus
extern "C" {
#endif

#include <ncursesw/ncurses.h>

struct panel_state;

int display_channels_locked(WINDOW *,struct panel_state *);

#ifdef __cplusplus
}
#endif

#endif
