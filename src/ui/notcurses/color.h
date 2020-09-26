#ifndef OMPHALOS_UI_NOTCURSES_COLOR
#define OMPHALOS_UI_NOTCURSES_COLOR

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

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

#define COLOR_MODBLUE	39
#define COLOR_MODGREY   234
#define COLOR_MODGREEN  47
#define COLOR_MODLIGHTBLUE	28
#define COLOR_MODLIGHTCYAN	44
#define COLOR_MODLIGHTGREY      242
#define COLOR_MODLIGHTGREEN     83
#define COLOR_MODLIGHTPURPLE    54
#define COLOR_MODLIGHTRED       124
#define	COLOR_MODVIOLET		93
#define COLOR_MODPURPLE         58
#define COLOR_MODRED            130
#define COLOR_MODYELLOW         210
#define COLOR_GREEN_75		46
#define COLOR_GREEN_50          40
#define COLOR_CHAMELEON		48
#define COLOR_CHAMELEON_75	41
#define COLOR_CHAMELEON_50	34
#define COLOR_SKYBLUE		33
#define COLOR_SKYBLUE_75	32
#define	COLOR_SKYBLUE_50	31
#define	COLOR_BLUE_75		38
#define	COLOR_BLUE_50		37
#define	COLOR_CYAN_75		27
#define	COLOR_CYAN_50		26
#define	COLOR_BONE		251
#define	COLOR_BONE_75		250
#define	COLOR_BONE_50		249
#define	COLOR_PALEALUMINIUM	231
#define COLOR_ORANGE		208
#define	COLOR_VIOLET_75		99
#define	COLOR_VIOLET_50		105

// Our color pairs
enum {
	BORDER_COLOR = 1,		// main window
	HEADER_COLOR,
	FOOTER_COLOR,
	DHEADING_COLOR,
	UHEADING_COLOR,
	PBORDER_COLOR,			// popups
	PHEADING_COLOR,
	BULKTEXT_COLOR,			// bulk text
	BULKTEXT_ALTROW_COLOR,
	IFACE_COLOR,			// interface summary text
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
	FIRST_FREE_COLOR
};

#ifdef __cplusplus
}
#endif

#endif
