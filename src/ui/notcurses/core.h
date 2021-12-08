#ifndef OMPHALOS_UI_NOTCURSES_CORE
#define OMPHALOS_UI_NOTCURSES_CORE

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdarg.h>
#include <notcurses/notcurses.h>

struct l4srv;
struct l2obj;
struct l2host;
struct l3host;
struct interface;
struct iface_state;
struct panel_state;
struct omphalos_packet;

struct panel_state {
  struct ncplane* n;
	int ysize;			// number of lines of *text* (not win)
};

extern struct ncreel *reel;
extern struct notcurses* NC;

// These functions may only be called while the notcurses lock is held. They
// themselves will not attempt to do any locking. Furthermore, they will not
// call screen_update() -- that is the caller's responsibility (in this way,
// multiple operations can be chained without screen updates, for flicker-free
// graphics).
int draw_main_window(struct ncplane *);
int setup_statusbar(int);
// FIXME can't use format attribute, see http://gcc.gnu.org/ml/gcc-patches/2001-12/msg01626.html
// __attribute__ ((format (printf,2,3)));
int wstatus_locked(struct ncplane *,const char *,...);
int wvstatus_locked(struct ncplane *w,const char *,va_list);
struct l4obj *service_callback_locked(const struct interface *, struct l2host *,
					struct l3host *, struct l4srv *);
struct l3obj *host_callback_locked(const struct interface *, struct l2host *,
					struct l3host *);
struct l2obj *neighbor_callback_locked(const struct interface *, struct l2host *);
void interface_removed_locked(struct ncreel *, struct iface_state *, struct panel_state **);
void *interface_cb_locked(struct ncreel *, struct interface *, struct iface_state *, struct panel_state *);
int packet_cb_locked(const struct interface *, struct omphalos_packet *, struct panel_state *);
void toggle_promisc_locked(struct ncplane *w);
void sniff_interface_locked(struct ncplane *w);
void down_interface_locked(struct ncplane *w);
void resolve_selection(struct ncplane *);
void reset_current_interface_stats(struct ncplane *);
void use_next_iface_locked(struct ncplane *, struct panel_state *);
void use_prev_iface_locked(struct ncplane *, struct panel_state *);

// Actions on the current interface
void use_next_node_locked(void);
void use_prev_node_locked(void);
void use_next_nodepage_locked(void);
void use_prev_nodepage_locked(void);
void use_first_node_locked(void);
void use_last_node_locked(void);
int expand_iface_locked(void);
int collapse_iface_locked(void);
// Select the current interface for host-granularity browsing (up and down now
// move within the interface rather than among interfaces).
int select_iface_locked(void);
// Go back to interface-granularity browsing.
int deselect_iface_locked(void);

// Subpanels
int update_diags_locked(struct panel_state *);
void hide_panel_locked(struct panel_state *ps);
int display_env_locked(struct ncplane *, struct panel_state *);
int display_help_locked(struct ncplane *, struct panel_state *);
int display_diags_locked(struct ncplane *, struct panel_state *);
int display_details_locked(struct ncplane *, struct panel_state *);
int new_display_panel(struct ncplane *, struct panel_state *,unsigned,unsigned,const wchar_t *);

#ifdef __cplusplus
}
#endif

#endif
