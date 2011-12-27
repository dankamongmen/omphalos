#include <assert.h>
#include <string.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/channels.h>

#define WIRELESSROWS 4 // FIXME

static int
channel_details(WINDOW *w,int rows){
	assert(wattrset(w,SUBDISPLAY_ATTR) == OK);
	const int col = START_COL;
	const int row = 1;
	int z;

	if((z = rows) > WIRELESSROWS){
		z = WIRELESSROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way through 0
		case WIRELESSROWS:{
			--z;
		}case 3:{
			--z;
		}case 2:{
			--z;
		}case 1:{
			--z;
		}case 0:{
			--z;
			break;
		}default:{
			return ERR;
		}
	}
	return OK;
}

int display_channels_locked(WINDOW *w,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(w,ps,WIRELESSROWS,0,L"press 'w' to dismiss display")){
	assert(0);
		goto err;
	}
	if(channel_details(panel_window(ps->p),ps->ysize)){
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
