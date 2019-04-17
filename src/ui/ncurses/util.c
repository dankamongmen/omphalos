#include <assert.h>
#include <string.h>
#include <ui/ncurses/util.h>

// Without the sides, these functions are very much faster. If you'll fill
// the interior, and can generate the sides yourself, go ahead and do that.
int bevel_bottom(WINDOW *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╰", },
		{ .attr = 0, .chars = L"╯", },
	};
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(rows && cols);
	//assert(mvwadd_wch(w,rows - 1,0,&bchr[0]) != ERR);
	assert(mvwhline(w,rows - 1,2,ACS_HLINE,cols - 2) != ERR);
	assert(mvwins_wch(w,rows - 1,cols - 1,&bchr[1]) != ERR);
	return OK;
}

int bevel_top(WINDOW *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╭", },
		{ .attr = 0, .chars = L"╮", },
	};
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(rows && cols);
	//assert(mvwadd_wch(w,0,0,&bchr[0]) != ERR);
	assert(mvwhline(w,0,1,ACS_HLINE,cols - 2) != ERR);
	assert(mvwins_wch(w,0,cols - 1,&bchr[1]) != ERR);
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
	assert(rows && cols);
	// called as one expects: 'mvwadd_wch(w,rows - 1,cols - 1,&bchr[3]);'
	// we get ERR returned. this is known behavior: fuck ncurses. instead,
	// we use mvwins_wch, which doesn't update the cursor position.
	// see http://lists.gnu.org/archive/html/bug-ncurses/2007-09/msg00001.html
	assert(mvwadd_wch(w,0,0,&bchr[0]) != ERR);
	assert(whline(w,ACS_HLINE,cols - 2) != ERR);
	assert(mvwins_wch(w,0,cols - 1,&bchr[1]) != ERR);
	if(rows > 1){
		assert(mvwvline(w,1,cols - 1,ACS_VLINE,rows - 1) != ERR);
		assert(mvwvline(w,1,0,ACS_VLINE,rows - 1) != ERR);
	}
	assert(mvwadd_wch(w,rows - 1,0,&bchr[2]) != ERR);
	assert(whline(w,ACS_HLINE,cols - 2) != ERR);
	assert(mvwins_wch(w,rows - 1,cols - 1,&bchr[3]) != ERR);
	return OK;
}

// For full safety, pass in a buffer that can hold the decimal representation
// of the largest uintmax_t plus three (one for the unit, one for the decimal
// separator, and one for the NUL byte). If omitdec is non-zero, and the
// decimal portion is all 0's, the decimal portion will not be printed. decimal
// indicates scaling, and should be '1' if no scaling has taken place.
const char *genprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,
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
