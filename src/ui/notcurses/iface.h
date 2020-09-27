#ifndef OMPHALOS_UI_NOTCURSES_IFACE
#define OMPHALOS_UI_NOTCURSES_IFACE

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <ui/notcurses/util.h>

struct l2obj;
struct l3obj;
struct l4obj;
struct l4srv;
struct l2host;
struct l3host;
struct reelbox;
struct interface;
struct iface_state;

enum {
	EXPANSION_NONE,
	EXPANSION_NODES,
	EXPANSION_HOSTS,
	EXPANSION_SERVICES
	// Update EXPANSION_MAX if you add one at the end
};

#define EXPANSION_MAX EXPANSION_SERVICES

// FIXME move this into iface.c
// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
// A box on the display reel. A box might be wholly visible, partially visible  endif
// but missing its bottom portion (bottom of the screen only), partially
// visible but missing its top portion (top of the screen only), wholly
// visible but split across the top/bottom sides, partially visible split across
// the top/bottom, and wholly invisible. A box is never drawn as larger than
// the main screen (ie, it will not appear partial while occupying the entirety
// of the main screen, even if it is logically larger).
//
// We never allow more than one line of blank space at the top of the screen,
// assuming there is data to be displayed. Blank space can never be at the top
// of the screen if there is less than one total screen of information.
//
// scrline is the logical location of the box's topmost line (border with name)
// on the containing pad (reel), taking positive values only. values larger
// than the screen's number of rows are meaningless for comparison purposes;
// ordering is then defined by the next and prev pointers alone (thus a
// possibly large number of offscreen interfaces needn't all be updated
// whenever locations change onscreen).
//
// If the first visible (lowest 'scrline' value) box has a scrline value
// greater than 1, then:
//
//  - if the value is 2 and the screen is full, things are fine.
//  - otherwise, make invisible boxes from the list visible in the blank space.
//  - if there is still space, use any invisible portions of the last box in
//     the list. if there is *still* space...
//  - there is less than a screen's worth of info. move all boxes up.
//
// Then display the boxes until scrline exceeds the number of rows in the
// containing screen, or we reach the end of the list.
//
// Get a reelbox's dimensions by calling getmaxyx() on its subwin member. These
// are the dimensions on the real screen, not the desirable dimensions on an // infinitely large screen.

typedef struct iface_state {
	struct interface *iface;	// corresponding omphalos iface struct
	const char *typestr;  // looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int devaction;			  // 1 == dowN, -1 == up, 0 == nothing
	int nodes;			      // number of nodes
	unsigned vnodes;		  // virtual nodecount (multicast etc)
	unsigned hosts;       // number of hosts (a node MAY have one
                        //  or more hosts; a host MUST have one
                        //  or more nodes)
	unsigned srvs;			  // number of services (same relations
					              //  to hosts as hosts have to nodes)
	struct l2obj *l2objs; // l2 entity list
	unsigned expansion;	  // degree of expansion/collapse
	int scrline;          // FIXME eliminate this; use getbegy()
	struct l2obj *selected;		// selected subentry
	int selline;          // line where the selection starts within the subwindow
  struct nctablet* tab; // nreel tablet with which we are associated
} iface_state;

struct iface_state *create_interface_state(struct interface *);
void free_iface_state(struct iface_state *);
void iface_box(const struct interface *i, const struct iface_state *is,
               struct ncplane *n, int active, int rows);
void print_iface_state(const struct interface *i, const struct iface_state *is,
                       struct ncplane *w, int rows, int cols, int active);
void print_iface_hosts(const struct interface *i, const iface_state *is,
                       struct ncplane *n, int rows, int cols,
                       bool drawfromtop, int active);
int lines_for_interface(const struct iface_state *);

struct l2obj *add_l2_to_iface(const struct interface *,struct iface_state *,struct l2host *);
struct l3obj *add_l3_to_iface(struct iface_state *,struct l2obj *,struct l3host *);
struct l4obj *add_service_to_iface(struct iface_state *,struct l2obj *,
				struct l3obj *,struct l4srv *,unsigned);

// Call after changing the degree of collapse/expansion, and resizing, but
// before redrawing.
void recompute_selection(iface_state *,int,int,int);

static inline int
iface_lines_bounded(const iface_state *is,int rows){
	int lnes = lines_for_interface(is);

	if(lnes > rows - 1){ // bottom border
		lnes = rows - 1;
	}
	return lnes;
}

static inline int
iface_lines_unbounded(const struct iface_state *is){
	return iface_lines_bounded(is,INT_MAX);
}

int selecting(void);

// Minimalist interface to data nodes from outside
struct l2obj *l2obj_next(struct l2obj *);
struct l2obj *l2obj_prev(struct l2obj *);
int l2obj_lines(const struct l2obj *);

#ifdef __cplusplus
}
#endif

#endif
