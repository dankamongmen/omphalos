#include <stdlib.h>
#include <string.h>
#include <ui/notcurses/util.h>
#include <ui/notcurses/color.h>
#include <ui/notcurses/envdisp.h>

#define ENVROWS 10
#define COLORSPERROW 32

static int
env_details(WINDOW *hw,int rows){
	const int col = START_COL;
	const int row = 1;
	int z,srows,scols;

	assert(wattrset(hw,SUBDISPLAY_ATTR) == OK);
	getmaxyx(stdscr,srows,scols);
	if((z = rows) >= ENVROWS){
		z = ENVROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (ENVROWS - 1):{
		while(z > 1){
			int c0,c1;

			c0 = (z - 2) * COLORSPERROW;
			c1 = c0 + (COLORSPERROW - 1);
			assert(mvwprintw(hw,row + z,col,"0x%02x--0x%02x: ",c0,c1) == OK);
			while(c0 <= c1){
				if(c0 < COLORS){
					assert(wattrset(hw,COLOR_PAIR(c0)) == OK);
					assert(wprintw(hw,"X") == OK);
				}else{
					assert(wattrset(hw,SUBDISPLAY_ATTR) == OK);
					assert(wprintw(hw," ") == OK);
				}
				++c0;
			}
			--z;
			assert(wattrset(hw,SUBDISPLAY_ATTR) == OK);
		}
		
	}	/* intentional fallthrough */
	case 1:{
		assert(mvwprintw(hw,row + z,col,"Colors (pairs): %u (%u) Geom: %dx%d",
				COLORS,COLOR_PAIRS,srows,scols) != ERR);
		--z;
	}	/* intentional fallthrough */
	case 0:{
		const char *lang = getenv("LANG");
		const char *term = getenv("TERM");

		lang = lang ? lang : "Undefined";
		assert(mvwprintw(hw,row + z,col,"LANG: %-21s TERM: %s ESCDELAY: %d",lang,term,ESCDELAY) != ERR);
		--z;
		break;
	}default:{
		return ERR;
	}
	}
	return OK;
}

int display_env_locked(WINDOW *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,ENVROWS,76,L"press 'e' to dismiss display")){
		goto err;
	}
	if(env_details(panel_window(ps->p),ps->ysize)){
		goto err;
	}
	return OK;

err:
	if(ps->p){
		WINDOW *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return ERR;
}
