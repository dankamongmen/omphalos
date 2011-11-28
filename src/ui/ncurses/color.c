#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <ui/ncurses/color.h>
#include <ncursesw/ncurses.h>

#define COLOR_CEILING 65536 // FIXME
#define COLORPAIR_CEILING 65536 // FIXME

// This exists because sometimes can_change_colors() will return 1, but then
// any actual attempt to change the colors via init_color() will return ERR :/.
int modified_colors = 0;

static int colors_allowed = -1,colorpairs_allowed = -1;
// Original color pairs. We don't change these in a fade.
static short ofg[COLORPAIR_CEILING],obg[COLORPAIR_CEILING];
// Original palette (after we've initialized it, ie pre-fading)
static short or[COLOR_CEILING],og[COLOR_CEILING],ob[COLOR_CEILING];
// Truly original palette (that taken from the terminal on startup)
static short oor[COLOR_CEILING],oog[COLOR_CEILING],oob[COLOR_CEILING];

int restore_colors(void){
	int ret = OK,q;

	if(colorpairs_allowed < 0 || colors_allowed < 0){
		return ERR;
	}
	// FIXME messes up gnome-terminal, whose palette we're hijacking by
	// default. most likely messes up all other terminals by being
	// commented out :/
	/*for(q = 0 ; q < colors_allowed ; ++q){
		ret |= init_color(q,oor[q],oog[q],oob[q]);
	}*/
	for(q = 0 ; q < colorpairs_allowed ; ++q){
		ret |= init_pair(q,ofg[q],obg[q]);
	}
	ret |= wrefresh(curscr);
	return ret;
}

// color_content() seems to give you the default ncurses value (one of 0, 680
// or 1000), *not* the actual value being used by the terminal... :/ This
// function is not likely useful until we can get the latter (we don't want
// generally to restore the (hideous) ncurses defaults).
int preserve_colors(void){
	int ret = OK,q;

	if(colorpairs_allowed >= 0 || colors_allowed >= 0){
		return ERR;
	}
	colors_allowed = COLORS;
	colorpairs_allowed = COLOR_PAIRS;
	if(colors_allowed > COLOR_CEILING || colorpairs_allowed > COLORPAIR_CEILING){
		return ERR;
	}
	for(q = 0 ; q < colorpairs_allowed ; ++q){
		ret |= pair_content(q,ofg + q,obg + q);
	}
	for(q = 0 ; q < colors_allowed ; ++q){
		ret |= color_content(q,oor + q,oog + q,oob + q);
	}
	return ret;
}

// Values from Ethan Schoonover's "Solarized" palette, sRGB space
#define RBLACK		0x00
#define GBLACK		0x2b
#define BBLACK		0x36
#define RWHITE		0xee
#define GWHITE		0xe8
#define BWHITE		0xd5
#define RBRWHITE	0xfd
#define GBRWHITE	0xf6
#define BBRWHITE	0xe3
#define RYELLOW		0xb5
#define GYELLOW		0x89
#define BYELLOW		0x00
#define RORANGE		0xcb
#define GORANGE		0x4b
#define BORANGE		0x16
#define RRED		0xdc
#define GRED		0x32
#define BRED		0x2f
#define RMAGENTA	0xd3
#define GMAGENTA	0x36
#define BMAGENTA	0x82
#define RVIOLET		0x6c
#define GVIOLET		0x71
#define BVIOLET		0xc4
#define RBLUE		0x26
#define GBLUE		0x8b
#define BBLUE		0xd2
#define RCYAN		0x2a
#define GCYAN		0xa1
#define BCYAN		0x98
#define RGREEN		0x85
#define GGREEN		0x99
#define BGREEN		0x00

// From the Tango Desktop Project
#define RCHAMELEON	0x8a
#define GCHAMELEON	0xe2
#define BCHAMELEON	0x34
#define RCHAMELEON1	0x73
#define GCHAMELEON1	0xd2
#define BCHAMELEON1	0x16
#define RCHAMELEON2	0x4e
#define GCHAMELEON2	0x9a
#define BCHAMELEON2	0x06
#define RSKYBLUE	0x72
#define GSKYBLUE	0x9f
#define BSKYBLUE	0xcf
#define RSKYBLUE1	0x34
#define GSKYBLUE1	0x65
#define BSKYBLUE1	0xa4
#define RSKYBLUE2	0x20
#define GSKYBLUE2	0x4a
#define BSKYBLUE2	0x87
#define RALUMINIUM	0x88
#define GALUMINIUM	0x8a
#define BALUMINIUM	0x85
#define RALUMINIUM1	0x55
#define GALUMINIUM1	0x57
#define BALUMINIUM1	0x53
#define RALUMINIUM2	0x2e
#define GALUMINIUM2	0x34
#define BALUMINIUM2	0x36

#define _BONE_R 142
#define _BONE_G 161
#define _BONE_B 152
#define CURSES_RGB(x) ((x) * 1000 / 255)
#define CURSES75_RGB(x) ((x) * 750 / 255)
#define CURSES50_RGB(x) ((x) * 500 / 255)
#define CURSES5_RGB(x) ((x) * 50 / 255)

// FIXME dark evil hackery aieeeee
int setup_extended_colors(void){
	int ret = OK,q;

	if(can_change_color() != TRUE){
		return ERR;
	}
	// rgb of 0->0, 85->333, 128->500, 170->666, 192->750, 255->999
	// Gnome-terminal palette:
	// #2E3436:#CC0000:#4E9A06:#C4A000:
	// #3465A4:#75507B:#06989A:#D3D7CF:
	// #555753:#EF2929:#8AE234:#FCE94F:
	// #729FCF:#AD7FA8:#34E2E2:#EEEEEC
	ret |= init_color(COLOR_BLACK,156,203,211);
	ret |= init_color(COLOR_RED,CURSES_RGB(RRED),CURSES_RGB(GRED),CURSES_RGB(BRED));
	ret |= init_color(COLOR_GREEN,CURSES_RGB(RGREEN),CURSES_RGB(GGREEN),CURSES_RGB(BGREEN));
	ret |= init_color(COLOR_YELLOW,CURSES_RGB(RYELLOW),CURSES_RGB(GYELLOW),CURSES_RGB(BYELLOW));
	ret |= init_color(COLOR_BLUE,CURSES_RGB(RBLUE),CURSES_RGB(GBLUE),CURSES_RGB(BBLUE));
	ret |= init_color(COLOR_MAGENTA,CURSES_RGB(RMAGENTA),CURSES_RGB(GMAGENTA),CURSES_RGB(BMAGENTA));
	ret |= init_color(COLOR_CYAN,CURSES_RGB(RCYAN),CURSES_RGB(GCYAN),CURSES_RGB(BCYAN));
	ret |= init_color(COLOR_WHITE,CURSES_RGB(RWHITE),CURSES_RGB(GWHITE),CURSES_RGB(BWHITE));
	ret |= init_color(8,332,340,324);
	ret |= init_color(9,933,160,160);
	ret |= init_color(COLOR_BRGREEN,539,882,203); // ncurses def
	ret |= init_color(11,983,909,308);
	ret |= init_color(COLOR_LIGHTBLUE,445,620,808); // ncurses def
	ret |= init_color(13,675,496,656);
	ret |= init_color(14,203,882,882);
	ret |= init_color(COLOR_BRIGHTWHITE,CURSES_RGB(RBRWHITE),CURSES_RGB(GBRWHITE),CURSES_RGB(BBRWHITE));
	ret |= init_color(COLOR_CYAN_75,CURSES75_RGB(RCYAN),CURSES75_RGB(GCYAN),CURSES75_RGB(BCYAN));
	ret |= init_color(COLOR_BLUE_75,CURSES75_RGB(RBLUE),CURSES75_RGB(GBLUE),CURSES75_RGB(BBLUE));
	ret |= init_color(COLOR_CYAN_50,CURSES50_RGB(RCYAN),CURSES50_RGB(GCYAN),CURSES50_RGB(BCYAN));
	ret |= init_color(COLOR_BLUE_50,CURSES50_RGB(RBLUE),CURSES50_RGB(GBLUE),CURSES50_RGB(BBLUE));
	ret |= init_color(COLOR_BONE,CURSES_RGB(_BONE_R),CURSES_RGB(_BONE_G),CURSES_RGB(_BONE_B));
	ret |= init_color(COLOR_VIOLET,CURSES_RGB(RVIOLET),CURSES_RGB(GVIOLET),CURSES_RGB(BVIOLET));
	ret |= init_color(COLOR_ORANGE,CURSES_RGB(RORANGE),CURSES_RGB(GORANGE),CURSES_RGB(BORANGE));
	ret |= init_color(COLOR_BONE_75,CURSES75_RGB(_BONE_R),CURSES75_RGB(_BONE_G),CURSES75_RGB(_BONE_B));
	ret |= init_color(COLOR_VIOLET_75,CURSES75_RGB(RVIOLET),CURSES75_RGB(GVIOLET),CURSES75_RGB(BVIOLET));
	ret |= init_color(COLOR_GREEN_75,CURSES75_RGB(RGREEN),CURSES75_RGB(GGREEN),CURSES75_RGB(BGREEN));
	ret |= init_color(COLOR_ORANGE_75,CURSES75_RGB(RORANGE),CURSES75_RGB(GORANGE),CURSES75_RGB(BORANGE));
	ret |= init_color(COLOR_MAGENTA_75,CURSES75_RGB(RMAGENTA),CURSES75_RGB(GMAGENTA),CURSES75_RGB(BMAGENTA));
	ret |= init_color(COLOR_BONE_50,CURSES50_RGB(_BONE_R),CURSES50_RGB(_BONE_G),CURSES50_RGB(_BONE_B));
	ret |= init_color(COLOR_VIOLET_50,CURSES50_RGB(RVIOLET),CURSES50_RGB(GVIOLET),CURSES50_RGB(BVIOLET));
	ret |= init_color(COLOR_GREEN_50,CURSES50_RGB(RGREEN),CURSES50_RGB(GGREEN),CURSES50_RGB(BGREEN));
	ret |= init_color(COLOR_MAGENTA_50,CURSES50_RGB(RMAGENTA),CURSES50_RGB(GMAGENTA),CURSES50_RGB(BMAGENTA));
	ret |= init_color(COLOR_PALEMAGENTA,CURSES5_RGB(RMAGENTA),CURSES5_RGB(GMAGENTA),CURSES5_RGB(BMAGENTA));
	ret |= init_color(COLOR_PALEORANGE,CURSES5_RGB(RORANGE),CURSES5_RGB(GORANGE),CURSES5_RGB(BORANGE));
	ret |= init_color(COLOR_PALEBLUE,CURSES5_RGB(RBLUE),CURSES5_RGB(GBLUE),CURSES5_RGB(BBLUE));
	ret |= init_color(COLOR_PALECYAN,CURSES5_RGB(RCYAN),CURSES5_RGB(GCYAN),CURSES5_RGB(BCYAN));
	ret |= init_color(COLOR_PALEVIOLET,CURSES5_RGB(RVIOLET),CURSES5_RGB(GVIOLET),CURSES5_RGB(BVIOLET));
	ret |= init_color(COLOR_PALEYELLOW,CURSES5_RGB(RYELLOW),CURSES5_RGB(GYELLOW),CURSES5_RGB(BYELLOW));
	ret |= init_color(COLOR_SKYBLUE,CURSES_RGB(RSKYBLUE),CURSES_RGB(GSKYBLUE),CURSES_RGB(BSKYBLUE));
	ret |= init_color(COLOR_SKYBLUE_75,CURSES75_RGB(RSKYBLUE),CURSES75_RGB(GSKYBLUE),CURSES75_RGB(BSKYBLUE));
	ret |= init_color(COLOR_SKYBLUE_50,CURSES50_RGB(RSKYBLUE),CURSES50_RGB(GSKYBLUE),CURSES50_RGB(BSKYBLUE));
	ret |= init_color(COLOR_CHAMELEON,CURSES_RGB(RCHAMELEON),CURSES_RGB(GCHAMELEON),CURSES_RGB(BCHAMELEON));
	ret |= init_color(COLOR_CHAMELEON_75,CURSES_RGB(RCHAMELEON2),CURSES_RGB(GCHAMELEON2),CURSES_RGB(BCHAMELEON2));
	ret |= init_color(COLOR_CHAMELEON_50,CURSES50_RGB(RCHAMELEON),CURSES50_RGB(GCHAMELEON),CURSES50_RGB(BCHAMELEON));
	ret |= init_color(COLOR_ALUMINIUM,CURSES_RGB(RALUMINIUM),CURSES_RGB(GALUMINIUM),CURSES_RGB(BALUMINIUM));
	ret |= init_color(COLOR_ALUMINIUM_75,CURSES75_RGB(RALUMINIUM),CURSES75_RGB(GALUMINIUM),CURSES75_RGB(BALUMINIUM));
	ret |= init_color(COLOR_ALUMINIUM_50,CURSES50_RGB(RALUMINIUM),CURSES50_RGB(GALUMINIUM),CURSES50_RGB(BALUMINIUM));
	ret |= wrefresh(curscr);
	if(ret == OK){
		modified_colors = 1;
	}
	for(q = 0 ; q < colors_allowed ; ++q){
		ret |= color_content(q,or + q,og + q,ob + q);
	}
	return ret;
}

static int
restore_our_colors(void){
	int ret = OK,q;

	if(colorpairs_allowed < 0 || colors_allowed < 0){
		return ERR;
	}
	for(q = 0 ; q < colors_allowed ; ++q){
		ret |= init_color(q,or[q],og[q],ob[q]);
	}
	ret |= wrefresh(curscr);
	return ret;
}

#include <unistd.h>
void fade(unsigned sec){
	short r[colors_allowed],g[colors_allowed],b[colors_allowed];
	// ncurses palettes are in terms of 0..1000, so there's no point in
	// trying to do more than 1000 iterations, ever. This is in usec.
	const long unsigned quanta = sec * 1000000 / 15;
	long unsigned sus,cus;
	struct timeval stime;
	int p;

	if(!modified_colors){
		return;
	}
	for(p = 0 ; p < colors_allowed ; ++p){
		r[p] = or[p];
		g[p] = og[p];
		b[p] = ob[p];
	}
	gettimeofday(&stime,NULL);
	cus = sus = stime.tv_sec * 1000000 + stime.tv_usec;
	while(cus < sus + sec * 1000000){
		long unsigned permille;
		struct timeval ctime;

		if((permille = (cus - sus) * 1000 / (sec * 1000000)) > 1000){
			permille = 1000;
		}
		for(p = 0 ; p < colors_allowed ; ++p){
			r[p] = (or[p] * (1000 - permille)) / 1000;
			g[p] = (og[p] * (1000 - permille)) / 1000;
			b[p] = (ob[p] * (1000 - permille)) / 1000;
			init_color(p,r[p],g[p],b[p]);
		}
		wrefresh(curscr);
		usleep(quanta);
		gettimeofday(&ctime,NULL);
		cus = ctime.tv_sec * 1000000 + ctime.tv_usec;
	}
	restore_our_colors();
	wrefresh(curscr);
}
