#ifndef OMPHALOS_UI_NCURSES_CHANNELS
#define OMPHALOS_UI_NCURSES_CHANNELS

#ifdef __cplusplus
extern "C" {
#endif

#include <ncursesw/ncurses.h>

struct iface_state;
struct panel_state;

int display_channels_locked(WINDOW *,struct panel_state *);

// Whenever a wireless device is added or removed, call these from the main
// callbacks.
int add_channel_support(struct iface_state *);
int del_channel_support(struct iface_state *);

#ifdef __cplusplus
}
#endif

#endif
