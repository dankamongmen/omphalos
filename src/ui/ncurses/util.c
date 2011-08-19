#include <assert.h>
#include <string.h>
#include <ui/ncurses/util.h>
#include <ncursesw/ncurses.h>

int bevel(WINDOW *w,int nobottom){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╭", },
		{ .attr = 0, .chars = L"╮", },
		{ .attr = 0, .chars = L"╰", },
		{ .attr = 0, .chars = L"╯", },
	};
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(box(w,0,0) != ERR);
	// called as one expects: 'mvwadd_wch(w,rows - 1,cols - 1,&bchr[3]);'
	// we get ERR returned. this is known behavior: fuck ncurses. instead,
	// we use mvwins_wch, which doesn't update the cursor position.
	// see http://lists.gnu.org/archive/html/bug-ncurses/2007-09/msg00001.html
	assert(mvwadd_wch(w,0,0,&bchr[0]) != ERR);
	assert(mvwins_wch(w,0,cols - 1,&bchr[1]) != ERR);
	if(!nobottom){
		assert(mvwadd_wch(w,rows - 1,0,&bchr[2]) != ERR);
		assert(mvwins_wch(w,rows - 1,cols - 1,&bchr[3]) != ERR);
	}else{
		assert(mvwadd_wch(w,rows - 1,0,WACS_VLINE) != ERR);
		assert(mvwins_wch(w,rows - 1,cols - 1,WACS_VLINE) != ERR);
	}
	return OK;
}

// For full safety, pass in a buffer that can hold the decimal representation
// of the largest uintmax_t plus three (one for the unit, one for the decimal
// separator, and one for the NUL byte). If omitdec is non-zero, and the
// decimal portion is all 0's, the decimal portion will not be printed. decimal
// indicates scaling, and should be '1' if no scaling has taken place.
char *genprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,
			int omitdec,unsigned mult,int uprefix){
	const char prefixes[] = "KMGTPEY";
	unsigned consumed = 0;
	uintmax_t div;

	div = mult;
	while((val / decimal) >= div && consumed < strlen(prefixes)){
		div *= mult;
		if(UINTMAX_MAX / div < mult){ // watch for overflow
			break;
		}
		++consumed;
	}
	if(div != mult){
		div /= mult;
		val /= decimal;
		if(val % div || omitdec == 0){
			snprintf(buf,bsize,"%ju.%02ju%c%c",val / div,(val % div) / ((div + 99) / 100),
					prefixes[consumed - 1],uprefix);
		}else{
			snprintf(buf,bsize,"%ju%c%c",val / div,prefixes[consumed - 1],uprefix);
		}
	}else{
		if(val % decimal || omitdec == 0){
			snprintf(buf,bsize,"%ju.%02ju",val / decimal,val % decimal);
		}else{
			snprintf(buf,bsize,"%ju",val / decimal);
		}
	}
	return buf;
}

#define REFRESH 60 // Screen refresh rate in Hz FIXME
#define SACRIFICE 8 // FIXME one more than COLOR_WHITE aieeeeeeee! terrible
#include <unistd.h>
void fade(unsigned sec){
	const unsigned quanta = REFRESH / 2; // max 1/2 of real refresh rate
	const unsigned us = sec * 1000000 / quanta; // period between shifts
	const int PAIR = BORDER_COLOR;
	short fg,bg,r,g,b,or,og,ob;
	int flip = 0;

	pair_content(PAIR,&fg,&bg);
	color_content(fg,&r,&g,&b);
	or = r;
	og = g;
	ob = b;
	while(r || g || b){
		r -= or / quanta;
		g -= og / quanta;
		b -= ob / quanta;
		r = r < 0 ? 0 : r;
		g = g < 0 ? 0 : g;
		b = b < 0 ? 0 : b;
		// Deep magic. We must convince ncurses that the screen changed,
		// or else nothing is repainted. Redefine the color pair,
		// flipping between the real color and a spare color.
		if(++flip % 2){
			init_color(fg,r,g,b);
			init_pair(PAIR,fg,bg);
		}else{
			init_color(SACRIFICE,r,g,b);
			init_pair(PAIR,SACRIFICE,bg);
		}
		wrefresh(curscr);
		usleep(us);
	}
	init_pair(PAIR,fg,bg);
}
