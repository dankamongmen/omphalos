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

// FIXME 680 is taken from "RGB_ON" in the ncurses sources
int setup_extended_colors(void){
	int ret = OK;

	if(can_change_color() != TRUE){
		return ERR;
	}
	ret |= init_color(COLOR_BOLDCYAN,0,999,999);
	ret |= init_color(COLOR_PURPLE,680 / 2,0,999);
	return ret;
}

#define REFRESH 60 // Screen refresh rate in Hz FIXME
#include <unistd.h>
void fade(unsigned sec){
	const unsigned us = sec * 1000000 / (REFRESH / 2); // max 1/2 of real
	short fg[MAX_OMPHALOS_COLOR],bg[MAX_OMPHALOS_COLOR];
	short or[COLOR_MAX],og[COLOR_MAX],ob[COLOR_MAX];
	short r[COLOR_MAX],g[COLOR_MAX],b[COLOR_MAX];
	unsigned quanta = sec * (REFRESH / 2),q;
	int p;

	for(p = 0 ; p < MAX_OMPHALOS_COLOR ; ++p){
		assert(pair_content(p,fg + p,bg + p) == OK);
	}
	for(p = 0 ; p < COLOR_MAX ; ++p){
		assert(color_content(p,r + p,g + p,b + p) == OK);
		or[p] = r[p];
		og[p] = g[p];
		ob[p] = b[p];
	}
	for(q = 0 ; q < quanta ; ++q){
		for(p = 0 ; p < COLOR_MAX ; ++p){
			r[p] -= or[p] / quanta;
			g[p] -= og[p] / quanta;
			b[p] -= ob[p] / quanta;
			r[p] = r[p] < 0 ? 0 : r[p];
			g[p] = g[p] < 0 ? 0 : g[p];
			b[p] = b[p] < 0 ? 0 : b[p];
		}
		for(p = 0 ; p < MAX_OMPHALOS_COLOR ; ++p){
			init_color(fg[p],r[fg[p]],g[fg[p]],b[fg[p]]);
		}
		wrefresh(curscr);
		usleep(us * 2);
	}
	for(p = 0 ; p < COLOR_MAX ; ++p){
		assert(init_color(p,or[p],og[p],ob[p]) == OK);
	}
	wrefresh(curscr);
}
