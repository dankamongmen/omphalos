#include <stdlib.h>
#include <string.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/color.h>
#include <ui/ncurses/envdisp.h>

#define ENVROWS 2 // FIXME

static int
env_details(WINDOW *hw,int rows){
	const int col = START_COL;
	const int row = 1;
	int z,srows,scols;

	getmaxyx(stdscr,srows,scols);
	if((z = rows) >= ENVROWS){
		z = ENVROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (ENVROWS - 1):{
		assert(mvwprintw(hw,row + z,col,"colors: "U64FMT"rows: "U32FMT"cols: "U32FMT"palette: %s",
				COLORS,srows,scols,modified_colors ? "dynamic" : "fixed") != ERR);
		--z;
	}case 0:{
		const char *lang = getenv("LANG");
		const char *term = getenv("TERM");

		lang = lang ? lang : "Undefined";
		assert(mvwprintw(hw,row + z,col,"LANG: %-21s TERM: %s",lang,term) != ERR);
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
	if(new_display_panel(mainw,ps,ENVROWS,0,L"press 'e' to dismiss display")){
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
