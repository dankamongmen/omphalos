#ifndef OMPHALOS_UI_NOTCURSES_NETWORK
#define OMPHALOS_UI_NOTCURSES_NETWORK

#ifdef __cplusplus
extern "C" {
#endif

struct panel_state;

int display_network_locked(struct ncplane *, struct panel_state *);
int update_network_details(struct ncplane *);

int display_bridging_locked(struct ncplane *, struct panel_state *);

#ifdef __cplusplus
}
#endif

#endif
