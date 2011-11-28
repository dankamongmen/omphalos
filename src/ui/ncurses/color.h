#ifndef OMPHALOS_UI_NCURSES_COLOR
#define OMPHALOS_UI_NCURSES_COLOR

#ifdef __cplusplus
extern "C" {
#endif

// Our additional colors
enum {
	COLOR_BRGREEN = 10,
	COLOR_LIGHTBLUE = 12,
	COLOR_BRIGHTWHITE = 15,
	COLOR_BLUE_75,
	COLOR_CYAN_75,
	COLOR_BLUE_50,
	COLOR_CYAN_50,
	COLOR_BONE,
	COLOR_BONE_75,
	COLOR_BONE_50,
	COLOR_PURPLE,
	COLOR_PURPLE_75,
	COLOR_PURPLE_50,
	COLOR_ORANGE,
	COLOR_ORANGE_75,
	COLOR_ORANGE_50,
	COLOR_BGREEN,
	COLOR_BGREEN_75,
	COLOR_BGREEN_50,
	COLOR_PALEORANGE,
	COLOR_PALEBLUE,
	COLOR_PALECYAN,
	COLOR_PALEPURPLE,
	COLOR_PALEYELLOW,
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
	BULKTEXT_COLOR,			// bulk text (help, details)
	IFACE_COLOR,			// interface summary text
	UCAST_COLOR,			// unicast addresses
	MCAST_COLOR,			// multicast addresses
	LCAST_COLOR,			// local addresses
	BCAST_COLOR,			// broadcast addresses
	UCAST_L3_COLOR,			// unicast l3
	LCAST_L3_COLOR,			// local l3
	MCAST_L3_COLOR,			// multicast l3
	BCAST_L3_COLOR,			// broadcast l3
	UCAST_RES_COLOR,		// unicast name
	LCAST_RES_COLOR,		// local name
	MCAST_RES_COLOR,		// multicast name
	BCAST_RES_COLOR,		// broadcast name
	UCAST_ALTROW_COLOR,
	MCAST_ALTROW_COLOR,
	LCAST_ALTROW_COLOR,
	BCAST_ALTROW_COLOR,
	UCAST_ALTROW_L3_COLOR,
	LCAST_ALTROW_L3_COLOR,
	MCAST_ALTROW_L3_COLOR,
	BCAST_ALTROW_L3_COLOR,
	UCAST_ALTROW_RES_COLOR,
	LCAST_ALTROW_RES_COLOR,
	MCAST_ALTROW_RES_COLOR,
	BCAST_ALTROW_RES_COLOR,
	LSELECTED_COLOR,		// selected node
	USELECTED_COLOR,
	MSELECTED_COLOR,
	BSELECTED_COLOR,
	SUBDISPLAY_COLOR,
	MAX_OMPHALOS_COLOR
};

int restore_colors(void);
int preserve_colors(void);

int setup_extended_colors(void);

void fade(unsigned);

extern int modified_colors;
// We only want to use bold when we couldn't define our own colors (otherwise,
// bold messes them up, and besides we can set colors as bold as we like).
#define OUR_BOLD (modified_colors ? 0 : A_BOLD)

#ifdef __cplusplus
}
#endif

#endif
