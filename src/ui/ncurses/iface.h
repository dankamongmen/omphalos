#ifndef OMPHALOS_UI_NCURSES_IFACE
#define OMPHALOS_UI_NCURSES_IFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <ncursesw/panel.h>
#include <ncursesw/ncurses.h>

struct l2host;
struct interface;

// FIXME move this inside iface.c if we can
typedef struct l2obj {
	struct l2obj *next;
	struct l2host *l2;
	int cat;			// cached result of l2categorize()
} l2obj;

// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
typedef struct iface_state {
	struct interface *iface;	// corresponding omphalos iface struct
	int scrline;			// line within the containing pad
	int ysize;			// number of lines
	int l2ents;			// number of l2 entities
	int first_visible;		// index of first visible l2 entity
	WINDOW *subwin;			// subwin
	PANEL *panel;			// panel
	const char *typestr;		// looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int devaction;			// 1 == down, -1 == up, 0 == nothing
	l2obj *l2objs;			// l2 entity list
	struct iface_state *next,*prev;
} iface_state;

int iface_box(WINDOW *,const struct interface *,const iface_state *,int);

#ifdef __cplusplus
}
#endif

#endif
