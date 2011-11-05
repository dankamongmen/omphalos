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

#define _BONE_R 142
#define _BONE_G 161
#define _BONE_B 152
#define _PURPLE_R 138
#define _PURPLE_G 139
#define _PURPLE_B 210
#define _BGREEN_R 55
#define _BGREEN_G 203
#define _BGREEN_B 75
#define _ORANGE_R 0xcb
#define _ORANGE_G 0x4b
#define _ORANGE_B 0x16
#define CURSES_RGB(x) ((x) * 1000 / 255)
#define CURSES75_RGB(x) ((x) * 750 / 255)
#define CURSES50_RGB(x) ((x) * 500 / 255)

// FIXME dark evil hackery aieeeee
int setup_extended_colors(void){
	int ret = OK,q;

	if(can_change_color() != TRUE){
		return ERR;
	}
	// rgb of 0->0, 85->333, 128->500, 170->666, 192->750, 255->999
	// Gnome-terminal palette:
#define GNOME_BLUE_R 203
#define GNOME_BLUE_G 394
#define GNOME_BLUE_B 640
#define GNOME75(x) ((x) * 3 / 4)
#define GNOME50(x) ((x) * 2 / 4)
	// #2E3436:#CC0000:#4E9A06:#C4A000:
	// #3465A4:#75507B:#06989A:#D3D7CF:
	// #555753:#EF2929:#8AE234:#FCE94F:
	// #729FCF:#AD7FA8:#34E2E2:#EEEEEC
	ret |= init_color(COLOR_BLACK,156,203,211);
	ret |= init_color(COLOR_RED,796,0,0);
	ret |= init_color(COLOR_GREEN,304,601,23);
	ret |= init_color(COLOR_YELLOW,765,624,0);
	ret |= init_color(COLOR_BLUE,GNOME_BLUE_R,GNOME_BLUE_G,GNOME_BLUE_B);
	ret |= init_color(COLOR_MAGENTA,457,312,480);
	ret |= init_color(COLOR_CYAN,23,593,601);
	ret |= init_color(COLOR_WHITE,823,839,808);
	ret |= init_color(8,332,340,324);
	ret |= init_color(9,933,160,160);
	ret |= init_color(10,539,882,203);
	ret |= init_color(11,983,909,308);
	ret |= init_color(12,445,620,808);
	ret |= init_color(13,675,496,656);
	ret |= init_color(14,203,882,882);
	ret |= init_color(15,929,929,921);
	ret |= init_color(COLOR_BLUE_75,GNOME75(GNOME_BLUE_R),GNOME75(GNOME_BLUE_G),GNOME75(GNOME_BLUE_B));
	ret |= init_color(COLOR_CYAN_75,17,445,451);
	ret |= init_color(COLOR_BLUE_50,GNOME50(GNOME_BLUE_R),GNOME50(GNOME_BLUE_G),GNOME50(GNOME_BLUE_B));
	ret |= init_color(COLOR_CYAN_50,17 * 2 / 3,445 * 2 / 3,451 * 2 /3);
	ret |= init_color(COLOR_BONE,CURSES_RGB(_BONE_R),CURSES_RGB(_BONE_G),CURSES_RGB(_BONE_B));
	ret |= init_color(COLOR_PURPLE,CURSES_RGB(_PURPLE_R),CURSES_RGB(_PURPLE_G),CURSES_RGB(_PURPLE_B));
	ret |= init_color(COLOR_BGREEN,CURSES_RGB(_BGREEN_R),CURSES_RGB(_BGREEN_G),CURSES_RGB(_BGREEN_B));
	ret |= init_color(COLOR_ORANGE,CURSES_RGB(_ORANGE_R),CURSES_RGB(_ORANGE_G),CURSES_RGB(_ORANGE_B));
	ret |= init_color(COLOR_BONE_75,CURSES75_RGB(_BONE_R),CURSES75_RGB(_BONE_G),CURSES75_RGB(_BONE_B));
	ret |= init_color(COLOR_PURPLE_75,CURSES75_RGB(_PURPLE_R),CURSES75_RGB(_PURPLE_G),CURSES75_RGB(_PURPLE_B));
	ret |= init_color(COLOR_BGREEN_75,CURSES75_RGB(_BGREEN_R),CURSES75_RGB(_BGREEN_G),CURSES75_RGB(_BGREEN_B));
	ret |= init_color(COLOR_ORANGE_75,CURSES75_RGB(_ORANGE_R),CURSES75_RGB(_ORANGE_G),CURSES75_RGB(_ORANGE_B));
	ret |= init_color(COLOR_BONE_50,CURSES50_RGB(_BONE_R),CURSES50_RGB(_BONE_G),CURSES50_RGB(_BONE_B));
	ret |= init_color(COLOR_PURPLE_50,CURSES50_RGB(_PURPLE_R),CURSES50_RGB(_PURPLE_G),CURSES50_RGB(_PURPLE_B));
	ret |= init_color(COLOR_BGREEN_50,CURSES50_RGB(_BGREEN_R),CURSES50_RGB(_BGREEN_G),CURSES50_RGB(_BGREEN_B));
	ret |= init_color(COLOR_ORANGE_50,CURSES50_RGB(_ORANGE_R),CURSES50_RGB(_ORANGE_G),CURSES50_RGB(_ORANGE_B));
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
		usleep(quanta);
		wrefresh(curscr);
		gettimeofday(&ctime,NULL);
		cus = ctime.tv_sec * 1000000 + ctime.tv_usec;
	}
	restore_our_colors();
	wrefresh(curscr);
}
