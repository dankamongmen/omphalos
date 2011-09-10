#ifndef OMPHALOS_UI_NCURSES_CORE
#define OMPHALOS_UI_NCURSES_CORE

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <ncursesw/panel.h>
#include <ncursesw/ncurses.h>

struct l2host;
struct l3host;
struct interface;
struct iface_state;
struct panel_state;
struct omphalos_iface;
struct omphalos_packet;

// FIXME we ought precreate the subwindows, and show/hide them rather than
// creating and destroying them every time.
struct panel_state {
	PANEL *p;
	int ysize;			// number of lines of *text* (not win)
};

int draw_main_window(WINDOW *);
int setup_statusbar(int);
int wstatus_locked(WINDOW *,const char *fmt,...);
int wvstatus_locked(WINDOW *w,const char *,va_list);
struct l3obj *host_callback_locked(const struct interface *,struct l2host *,struct l3host *);
struct l2obj *neighbor_callback_locked(const struct interface *,struct l2host *);
void interface_removed_locked(struct iface_state *,struct panel_state *);
void *interface_cb_locked(struct interface *,struct iface_state *,struct panel_state *);
int packet_cb_locked(const struct interface *,struct omphalos_packet *,struct panel_state *);
void toggle_promisc_locked(const struct omphalos_iface *,WINDOW *w);
void sniff_interface_locked(const struct omphalos_iface *,WINDOW *w);
void down_interface_locked(const struct omphalos_iface *,WINDOW *w);
void hide_panel_locked(struct panel_state *ps);
int display_network_locked(WINDOW *,struct panel_state *);
int display_details_locked(WINDOW *,struct panel_state *);
int new_display_panel(WINDOW *,struct panel_state *,int,int,const wchar_t *);
void reset_all_interface_stats(WINDOW *);
void reset_current_interface_stats(WINDOW *);
void use_next_iface_locked(WINDOW *,struct panel_state *);
void use_prev_iface_locked(WINDOW *,struct panel_state *);
int expand_iface_locked(void);
int collapse_iface_locked(void);

#ifdef __cplusplus
}
#endif

#endif
