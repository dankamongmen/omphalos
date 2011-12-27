#include <assert.h>
#include <string.h>
#include <net/if.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <omphalos/wireless.h>
#include <ui/ncurses/channels.h>

#define WIRELESSROWS 4 // FIXME

// We take advantage of the fact that bgn, an, and y all support multiples of
// 14 channels to avoid doing a fully dynamic layout. Unfortunately, this
// assumes at least 70 columns (14 * 4 + IFNAMSIZ + 1).
#define FREQSPERROW 14	// FIXME do it dynamically based on cols

static int
channel_row(WINDOW *w,unsigned freqrow,int srow,int scol){
	unsigned f;

	assert(wmove(w,srow,scol) == OK);
	for(f = freqrow * FREQSPERROW ; f < (freqrow + 1) * FREQSPERROW ; ++f){
		unsigned chan = wireless_chan_byidx(f);

		assert(chan && chan < 1000);
		assert(wprintw(w," %3u",chan) == OK);
	}
	return 0;
}

static int
channel_details(WINDOW *w){
	unsigned freqs,freqrows,z;
	const int col = START_COL;
	const int row = 1;
	int r,c;

	getmaxyx(w,r,c);
	assert(c >= FREQSPERROW * 4 + IFNAMSIZ + 1); // FIXME see above
	assert(wattrset(w,SUBDISPLAY_ATTR) == OK);
	freqs = wireless_freq_count();
	assert(freqs % FREQSPERROW == 0);
	freqrows = freqs / FREQSPERROW;
	if((z = r) >= WIRELESSROWS){
		z = WIRELESSROWS - 1;
	}
	assert(z >= freqrows); // FIXME scroll display
	switch(z){ // Intentional fallthroughs all the way through 0
		case (WIRELESSROWS - 1):{
			channel_row(w,freqrows - 1,row + z,col + IFNAMSIZ + 1);
			--z;
		}case 2:{
			channel_row(w,freqrows - 2,row + z,col + IFNAMSIZ + 1);
			--z;
		}case 1:{
			channel_row(w,freqrows - 3,row + z,col + IFNAMSIZ + 1);
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
		goto err;
	}
	if(channel_details(panel_window(ps->p))){
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
