#include <assert.h>
#include <string.h>
#include <ui/ncurses/util.h>
#include <ncursesw/ncurses.h>

#define COLOR_CEILING 256 // FIXME

static short or[COLOR_CEILING],og[COLOR_CEILING],ob[COLOR_CEILING];

int bevel_notop(WINDOW *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╰", },
		{ .attr = 0, .chars = L"╯", },
	};
	int rows,cols;

	getmaxyx(w,rows,cols);
	if(rows > 1){
		assert(mvwvline(w,0,0,ACS_VLINE,rows - 1) != ERR);
		assert(mvwvline(w,0,cols - 1,ACS_VLINE,rows - 1) != ERR);
	}
	assert(mvwadd_wch(w,rows - 1,0,&bchr[0]) != ERR);
	assert(mvwhline(w,rows - 1,1,ACS_HLINE,cols - 2) != ERR);
	assert(mvwins_wch(w,rows - 1,cols - 1,&bchr[1]) != ERR);
	return OK;
}

int bevel_nobottom(WINDOW *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╭", },
		{ .attr = 0, .chars = L"╮", },
	};
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(mvwadd_wch(w,0,0,&bchr[0]) != ERR);
	assert(mvwins_wch(w,0,cols - 1,&bchr[1]) != ERR);
	assert(mvwhline(w,0,1,ACS_HLINE,cols - 2) != ERR);
	if(rows > 1){
		assert(mvwvline(w,1,0,ACS_VLINE,rows - 1) != ERR);
		assert(mvwvline(w,1,cols - 1,ACS_VLINE,rows - 1) != ERR);
	}
	return OK;
}

int bevel(WINDOW *w){
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
	assert(mvwadd_wch(w,rows - 1,0,&bchr[2]) != ERR);
	assert(mvwins_wch(w,rows - 1,cols - 1,&bchr[3]) != ERR);
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
	int ret = OK,q;

	assert(COLORS <= COLOR_CEILING);
	if(can_change_color() != TRUE){
		return ERR;
	}
	// rgb of 0->0, 85->333, 128->500, 170->666, 192->750, 255->999
	// Gnome-terminal palette:
	// 06 -> 23
	// 20 -> 125
	// 29 -> 160
	// 2E -> 156
	// 34 -> 203
	// 36 -> 211
	// 4e -> 304
	// 50 -> 312
	// 53 -> 324
	// 55 -> 332
	// 57 -> 340
	// 65 -> 394
	// 75 -> 457
	// 7b -> 480
	// 8a -> 539
	// 9a -> 601
	// a0 -> 624
	// a4 -> 640
	// c4 -> 765
	// CC -> 796
	// cf -> 808
	// d3 -> 823
	// d7 -> 839
	// e2 -> 882
	// ef -> 933
	// #2E3436:#CC0000:#4E9A06:#C4A000:
	// #3465A4:#75507B:#06989A:#D3D7CF:
	// #555753:#EF2929:#8AE234:#FCE94F:
	// #729FCF:#AD7FA8:#34E2E2:#EEEEEC
	ret |= init_color(COLOR_BLACK,156,203,211);
	ret |= init_color(COLOR_RED,796,0,0);
	ret |= init_color(COLOR_GREEN,304,601,23);
	ret |= init_color(COLOR_YELLOW,765,624,0);
	ret |= init_color(COLOR_BLUE,203,394,640);
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
	for(q = 0 ; q < COLORS ; ++q){
		assert(color_content(q,or + q,og + q,ob + q) == OK);
	}
	ret |= wrefresh(curscr);
	return ret;
}

#define REFRESH 60 // Screen refresh rate in Hz FIXME
#include <unistd.h>
void fade(unsigned sec){
	const int quanta = sec * (REFRESH / 4);
	const int us = sec * 1000000 / quanta;
	short r[COLORS],g[COLORS],b[COLORS];
	int q;

	assert(COLORS <= COLOR_CEILING); // FIXME use number collected originally
	for(q = 0 ; q < COLORS ; ++q){
		r[q] = or[q];
		g[q] = og[q];
		b[q] = ob[q];
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
	/*for(q = 0 ; q < COLORS ; ++q){
		assert(init_color(q,or[q],og[q],ob[q]) == OK);
	}*/
	setup_extended_colors();
	wrefresh(curscr);
}
