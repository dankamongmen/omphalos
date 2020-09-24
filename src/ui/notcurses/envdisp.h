#ifndef OMPHALOS_UI_NOTCURSES_ENVDISP
#define OMPHALOS_UI_NOTCURSES_ENVDISP

#ifdef __cplusplus
extern "C" {
#endif

#include <ui/notcurses/core.h>

struct panel_state;

int display_env_locked(WINDOW *,struct panel_state *);

#ifdef __cplusplus
}
#endif

#endif
