#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <omphalos/popen.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/color.h>
#include <ncursesw/ncurses.h>

#define COLOR_CEILING 65536 // FIXME
#define COLORPAIR_CEILING 65536 // FIXME

// This exists because sometimes can_change_color() will return 1, but then
// any actual attempt to change the colors via init_color() will return ERR :/.
int modified_colors = 0;

static int colors_allowed = -1,colorpairs_allowed = -1;
// Original palette (after we've initialized it, ie pre-fading)
static short or[COLOR_CEILING],og[COLOR_CEILING],ob[COLOR_CEILING];
// Truly original palette (that taken from the terminal on startup)
static short oor[COLOR_CEILING],oog[COLOR_CEILING],oob[COLOR_CEILING];

int restore_colors(void){
	int ret = OK,q;

	if(colorpairs_allowed < 0 || colors_allowed < 0){
		return ERR;
	}
	for(q = RESERVED_COLORS ; q < colors_allowed ; ++q){
		ret |= init_color(q,oor[q],oog[q],oob[q]);
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
	for(q = 0 ; q < colors_allowed ; ++q){
		ret |= color_content(q,oor + q,oog + q,oob + q);
	}
	if(ret){
		wstatus_locked(stdscr,"Couldn't get palette from Ncurses configuration");
	}
	return ret;
}

int setup_extended_colors(void){
	int ret = OK,q;

	if(can_change_color() != TRUE){
		return ERR;
	}
	// rgb of 0->0, 85->333, 128->500, 170->666, 192->750, 255->999
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
	for(q = RESERVED_COLORS ; q < colors_allowed ; ++q){
		ret |= init_color(q,or[q],og[q],ob[q]);
	}
	ret |= wrefresh(curscr);
	return ret;
}

struct marsh {
	unsigned sec;
	pthread_mutex_t *lock;
};

static void *
fade_thread(void *unsafe_marsh){
	struct marsh *marsh = unsafe_marsh;
	short r[colors_allowed],g[colors_allowed],b[colors_allowed];
	// ncurses palettes are in terms of 0..1000, so there's no point in
	// trying to do more than 1000 iterations, ever. This is in usec.
	const long unsigned quanta = marsh->sec * 1000000 / 15;
	long unsigned sus,cus;
	struct timeval stime;
	int p;

	for(p = RESERVED_COLORS ; p < colors_allowed ; ++p){
		r[p] = or[p];
		g[p] = og[p];
		b[p] = ob[p];
	}
	gettimeofday(&stime,NULL);
	cus = sus = stime.tv_sec * 1000000 + stime.tv_usec;
	while(cus < sus + marsh->sec * 1000000){
		long unsigned permille;
		struct timeval ctime;

		if((permille = (cus - sus) * 1000 / (marsh->sec * 1000000)) > 1000){
			permille = 1000;
		}
		for(p = RESERVED_COLORS ; p < colors_allowed ; ++p){
			r[p] = (or[p] * (1000 - permille)) / 1000;
			g[p] = (og[p] * (1000 - permille)) / 1000;
			b[p] = (ob[p] * (1000 - permille)) / 1000;
		}
		pthread_mutex_lock(marsh->lock);
			for(p = RESERVED_COLORS ; p < colors_allowed ; ++p){
				init_color(p,r[p],g[p],b[p]);
			}
			wrefresh(curscr);
		pthread_mutex_unlock(marsh->lock);
		usleep(quanta);
		gettimeofday(&ctime,NULL);
		cus = ctime.tv_sec * 1000000 + ctime.tv_usec;
	}
	pthread_mutex_lock(marsh->lock);
		restore_our_colors();
		wrefresh(curscr);
	pthread_mutex_unlock(marsh->lock);
	free(marsh);
	pthread_exit(NULL);
}

int fade(unsigned sec,pthread_mutex_t *lock,pthread_t *tid){
	struct marsh *marsh;

	if(!modified_colors){
		return -1;
	}
	if((marsh = malloc(sizeof(*marsh))) == NULL){
		return -1;
	}
	marsh->sec = sec;
	marsh->lock = lock;
	if(pthread_create(tid,NULL,fade_thread,marsh)){
		free(marsh);
		return -1;
	}
	return 0;
}
