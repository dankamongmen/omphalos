#ifndef OMPHALOS_UI_NOTCURSES_NETWORK
#define OMPHALOS_UI_NOTCURSES_NETWORK

#ifdef __cplusplus
extern "C" {
#endif

#include <ui/notcurses/core.h>

struct panel_state;

int display_network_locked(struct ncplane *stdn, struct panel_state *);
int update_network_details(struct ncplane *stdn);

int display_bridging_locked(struct ncplane *stdn, struct panel_state *);

#ifdef __cplusplus
}
#endif

#endif
