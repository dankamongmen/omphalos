#ifndef OMPHALOS_UI_NCURSES_COLOR
#define OMPHALOS_UI_NCURSES_COLOR

#ifdef __cplusplus
extern "C" {
#endif

// Our additional colors
enum {
	COLOR_BLUE_75 = 16,
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
	ROUTER_COLOR,			// routing l3 addresses / access points
	UCAST_L3_COLOR,			// unicast l3
	LCAST_L3_COLOR,			// local l3
	MCAST_L3_COLOR,			// multicast l3
	BCAST_L3_COLOR,			// broadcast l3
	UCAST_RES_COLOR,		// unresolved unicast l3
	LCAST_RES_COLOR,		// unresolved local l3
	MCAST_RES_COLOR,		// unresolved multicast l3
	BCAST_RES_COLOR,		// unresolved broadcast l3
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
