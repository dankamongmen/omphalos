#ifndef OMPHALOS_UI_NCURSES_COLOR
#define OMPHALOS_UI_NCURSES_COLOR

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

extern const char *palsource;

// There are 16 colors in generic use, which we mustn't change across the life
// of our program (we could if we could reliably acquire the active palette
// upon startup, but this turns out to be impossible to do portably). We thus
// don't modify those color registers, even if we can. If only 16 colors are
// supported, we thus do not define any colors (and will not do any other
// palette tricks, including fades).
//
// If we support more than 16 colors, we'll attempt to modify some higher ones.
// If we can't modify them, we just use the 16 colors we came in with, as we
// assume they form a reasonable palette (and not, for instance, 16 shades of
// one color, as is common in 256-color shell palettes). These colors are only
// to be used in that case, and *only* these colors ought be used (so that
// our colors are deterministic, and so that we can fade them reliably).

#define RESERVED_COLORS 16

enum {
	COLOR_MODBLACK = 16,
	COLOR_MODBLUE,
	COLOR_MODBROWN,
	COLOR_MODCYAN,
	COLOR_MODGREY,
	COLOR_MODGREEN,
	COLOR_MODLIGHTGREY,
	COLOR_MODLIGHTBLUE,
	COLOR_MODLIGHTCYAN,
	COLOR_MODLIGHTGREEN,
	COLOR_MODLIGHTPURPLE,
	COLOR_MODLIGHTRED,
	COLOR_MODPURPLE,
	COLOR_MODRED,
	COLOR_MODWHITE,
	COLOR_MODYELLOW,
	COLOR_GREEN_75,
	COLOR_GREEN_50,
	COLOR_BLUE_75,
	COLOR_BLUE_50,
	COLOR_CYAN_75,
	COLOR_CYAN_50,
	COLOR_BONE,
	COLOR_BONE_75,
	COLOR_BONE_50,
	COLOR_MODVIOLET,
	COLOR_VIOLET_75,
	COLOR_VIOLET_50,
	COLOR_ORANGE,
	COLOR_ORANGE_75,
	COLOR_ORANGE_50,
	COLOR_MAGENTA_50,
	COLOR_MAGENTA_75,
	COLOR_PALEORANGE,
	COLOR_PALEBLUE,
	COLOR_PALECYAN,
	COLOR_PALEVIOLET,
	COLOR_PALEYELLOW,
	COLOR_PALEMAGENTA,
	COLOR_PALEALUMINIUM,
	COLOR_SKYBLUE,
	COLOR_SKYBLUE_75,
	COLOR_SKYBLUE_50,
	COLOR_CHAMELEON,
	COLOR_CHAMELEON_75,
	COLOR_CHAMELEON_50,
	COLOR_ALUMINIUM,
	COLOR_ALUMINIUM_75,
	COLOR_ALUMINIUM_50,
};

// Our color pairs
enum {
	BORDER_COLOR = 1,		// main window
	HEADER_COLOR,
	FOOTER_COLOR,
	DBORDER_COLOR,			// down interfaces
	DHEADING_COLOR,
	UBORDER_COLOR,			// up interfaces
	UHEADING_COLOR,
	PBORDER_COLOR,			// popups
	PHEADING_COLOR,
	BULKTEXT_COLOR,			// bulk text
	BULKTEXT_ALTROW_COLOR,
	IFACE_COLOR,			// interface summary text
	UCAST_COLOR,			// unicast addresses
	MCAST_COLOR,			// multicast addresses
	LCAST_COLOR,			// local addresses
	BCAST_COLOR,			// broadcast addresses
	UCAST_L3_COLOR,			// unicast l3
	UCAST_RES_COLOR,		// unicast name
	UCAST_ALTROW_COLOR,
	UCAST_ALTROW_L3_COLOR,
	UCAST_ALTROW_RES_COLOR,
	USELECTED_COLOR,
	LCAST_L3_COLOR,			// local l3
	LCAST_RES_COLOR,		// local name
	LCAST_ALTROW_COLOR,
	LCAST_ALTROW_L3_COLOR,
	LCAST_ALTROW_RES_COLOR,
	LSELECTED_COLOR,		// selected node
	MCAST_L3_COLOR,			// multicast l3
	MCAST_RES_COLOR,		// multicast name
	MCAST_ALTROW_COLOR,
	MCAST_ALTROW_L3_COLOR,
	MCAST_ALTROW_RES_COLOR,
	MSELECTED_COLOR,
	BCAST_L3_COLOR,			// broadcast l3
	BCAST_RES_COLOR,		// broadcast name
	BCAST_ALTROW_COLOR,
	BCAST_ALTROW_L3_COLOR,
	BCAST_ALTROW_RES_COLOR,
	BSELECTED_COLOR,
	SUBDISPLAY_COLOR,
	MAX_OMPHALOS_COLOR
};

int restore_colors(void);

int preserve_colors(void);

int setup_extended_colors(void);

int fade(unsigned,pthread_mutex_t *,pthread_t *);

extern int modified_colors;
// We only want to use bold when we couldn't define our own colors (otherwise,
// bold messes them up, and besides we can set colors as bold as we like).
#define OUR_BOLD (modified_colors ? 0 : A_BOLD)

#ifdef __cplusplus
}
#endif

#endif
