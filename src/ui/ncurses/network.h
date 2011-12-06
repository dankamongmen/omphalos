#ifndef OMPHALOS_UI_NCURSES_NETWORK
#define OMPHALOS_UI_NCURSES_NETWORK

#ifdef __cplusplus
extern "C" {
#endif

#include <ncursesw/ncurses.h>

struct panel_state;

int display_network_locked(WINDOW *,struct panel_state *);
int update_network_details(WINDOW *);

#ifdef __cplusplus
}
#endif

#endif
