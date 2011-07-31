#ifndef OMPHALOS_UI_NCURSES_IFACE
#define OMPHALOS_UI_NCURSES_IFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <ncursesw/panel.h>
#include <ncursesw/ncurses.h>

struct l2obj;
struct l2host;
struct interface;

// FIXME move this inside iface.c if we can
// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
typedef struct iface_state {
	struct interface *iface;	// corresponding omphalos iface struct
	int scrline;			// line within the containing pad
	int l2ents;			// number of l2 entities
	WINDOW *subwin;			// subwin
	PANEL *panel;			// panel
	const char *typestr;		// looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int devaction;			// 1 == down, -1 == up, 0 == nothing
	struct l2obj *l2objs;		// l2 entity list
	struct iface_state *next,*prev;
} iface_state;

// FIXME also try to move this
int iface_box(WINDOW *,const struct interface *,const iface_state *,int);

struct l2obj *add_l2_to_iface(const struct interface *,iface_state *,struct l2host *);
int print_iface_hosts(const struct interface *,const iface_state *);
int print_iface_state(const struct interface *,const iface_state *);
void free_iface_state(iface_state *);

#ifdef __cplusplus
}
#endif

#endif
