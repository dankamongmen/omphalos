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
struct reelbox;
struct interface;
struct iface_state;

// FIXME move this into iface.c
// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
typedef struct iface_state {
	struct interface *iface;	// corresponding omphalos iface struct
	const char *typestr;		// looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int devaction;			// 1 == down, -1 == up, 0 == nothing
	unsigned nodes;			// number of nodes
	unsigned hosts;			// number of hosts (a node MAY have one
					//  or more hosts; a host MUST have one
					//  or more nodes)
	unsigned srvs;			// number of services (same relations
					//  to hosts as hosts have to nodes)
	struct l2obj *l2objs;		// l2 entity list
	unsigned expansion;		// degree of expansion/collapse
	struct iface_state *next,*prev;	// circular list; all ifaces are here
	struct reelbox *rb;		// our reelbox (UI elements). if we're
					// entirely offscreen, this is NULL.
} iface_state;

typedef struct service {
} service;

int redraw_iface(const struct iface_state *,const struct reelbox *,int);

struct iface_state *create_interface_state(struct interface *);
void free_iface_state(struct iface_state *);

int iface_visible_p(int,const struct reelbox *);
int iface_wholly_visible_p(int,const struct reelbox *);
int lines_for_interface(const struct iface_state *);
void move_interface(struct reelbox *,int,int,int,int);

struct l2obj *add_l2_to_iface(const struct interface *,struct iface_state *,struct l2host *);
struct l3obj *add_l3_to_iface(struct iface_state *,struct l2obj *,struct l3host *);
struct l4obj *add_service_to_iface(struct iface_state *,struct l3obj *,const service *);

void expand_interface(struct iface_state *);
void collapse_interface(struct iface_state *);

#ifdef __cplusplus
}
#endif

#endif
