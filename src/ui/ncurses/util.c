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

// FIXME 680 is taken from "RGB_ON" in the ncurses sources. we're using
// standard vga colors explicitly.
// FIXME dark evil hackery aieeeee
int setup_extended_colors(void){
	int ret = OK;

	if(can_change_color() != TRUE){
		return ERR;
	}
	// rgb of 0->0, 85->333, 128->500, 170->666, 192->750, 255->999
	ret |= init_color(COLOR_BLACK,180,180,180);
	ret |= init_color(COLOR_RED,666,0,0);
	ret |= init_color(COLOR_BLUE,0,333,750);
	ret |= init_color(COLOR_GREEN,0,500,0);
	ret |= init_color(9,999,333,333);
	ret |= init_color(10,333,999,333);
	ret |= init_color(12,333,333,999);
	ret |= wrefresh(curscr);
	return ret;
}

#define REFRESH 60 // Screen refresh rate in Hz FIXME
#include <unistd.h>
void fade(unsigned sec){
	const unsigned quanta = sec * (REFRESH / 4);
	const unsigned us = sec * 1000000 / quanta;
	short or[COLORS],og[COLORS],ob[COLORS];
	short r[COLORS],g[COLORS],b[COLORS];
	unsigned q;

	for(q = 0 ; q < sizeof(or) / sizeof(*or) ; ++q){
		assert(color_content(q,r + q,g + q,b + q) == OK);
		or[q] = r[q];
		og[q] = g[q];
		ob[q] = b[q];
	}
	for(q = 0 ; q < quanta ; ++q){
		unsigned p;

		for(p = 0 ; p < sizeof(r) / sizeof(*r) ; ++p){
			r[p] -= or[p] / quanta;
			g[p] -= og[p] / quanta;
			b[p] -= ob[p] / quanta;
			r[p] = r[p] < 0 ? 0 : r[p];
			g[p] = g[p] < 0 ? 0 : g[p];
			b[p] = b[p] < 0 ? 0 : b[p];
			init_color(p,r[p],g[p],b[p]);
		}
		wrefresh(curscr);
		usleep(us);
		// We ought feed back the actual time interval and perhaps
		// fade more rapidly based on the result. This ought control
		// flicker in all circumstances, becoming a single palette fade
		// in the limit (ie, no fade at all).
	}
	// FIXME also want all other windows cleared. best to interleave the
	// fade with actual interface shutdown so they're naturally gone
	for(q = 0 ; q < sizeof(or) / sizeof(*or) ; ++q){
		assert(init_color(q,or[q],og[q],ob[q]) == OK);
	}
	setup_extended_colors();
	wrefresh(curscr);
}
