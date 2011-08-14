#ifndef OMPHALOS_UI_NCURSES_IFACE
#define OMPHALOS_UI_NCURSES_IFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <ncursesw/panel.h>
#include <ncursesw/ncurses.h>

struct l2obj;
struct l3obj;
struct l2host;
struct l3host;
struct interface;
struct iface_state;

// FIXME move this into iface.c
// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
typedef struct iface_state {
	struct interface *iface;	// corresponding omphalos iface struct
	int scrline;			// line within the containing pad
	WINDOW *subwin;			// subwin
	PANEL *panel;			// panel
	const char *typestr;		// looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int devaction;			// 1 == down, -1 == up, 0 == nothing
	unsigned nodes;			// number of nodes
	unsigned hosts;			// number of hosts (a node MAY have one
					//  or more hosts; a host MUST have one
					//  or more nodes)
	struct l2obj *l2objs;		// l2 entity list
	struct iface_state *next,*prev;
} iface_state;

// FIXME also try to move this
void iface_box(WINDOW *,const struct interface *,const struct iface_state *,int);
void redraw_iface(const struct interface *,const struct iface_state *,int);

struct iface_state *create_interface_state(struct interface *);
void update_iface_state(const struct iface_state *);
void free_iface_state(struct iface_state *);

int iface_wholly_visible_p(int,const struct iface_state *);
int lines_for_interface(const struct iface_state *);
int move_interface(struct iface_state *,int,int,int);

struct l2obj *add_l2_to_iface(const struct interface *,struct iface_state *,struct l2host *);
struct l3obj *add_l3_to_iface(struct iface_state *,struct l2obj *,struct l3host *);

#ifdef __cplusplus
}
#endif

#endif
