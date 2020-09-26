#include <assert.h>
#include <string.h>
#include <ui/notcurses/util.h>

// Without the sides, these functions are very much faster. If you'll fill
// the interior, and can generate the sides yourself, go ahead and do that.
int bevel_bottom(struct ncplane *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╰", },
		{ .attr = 0, .chars = L"╯", },
	};
	int rows,cols;

	ncplane_dim_yx(w, &rows, &cols);
	assert(rows > 0 && cols > 0);
	//assert(mvwadd_wch(w,rows - 1,0,&bchr[0]) != ERR);
	mvwhline(w, rows - 1, 2, ACS_HLINE, cols - 2);
	mvwins_wch(w, rows - 1, cols - 1, &bchr[1]);
	return 0;
}

int bevel_top(struct ncplane *w){
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
	return 0;
}

int bevel(struct ncplane *w){
	static const cchar_t bchr[] = {
		{ .attr = 0, .chars = L"╭", },
		{ .attr = 0, .chars = L"╮", },
		{ .attr = 0, .chars = L"╰", },
		{ .attr = 0, .chars = L"╯", },
	};
	int rows, cols;

	ncplane_dim_yx(w, &rows, &cols);
	assert(rows > 0 && cols > 0);
	// called as one expects: 'mvwadd_wch(w,rows - 1,cols - 1,&bchr[3]);'
	// we get ERR returned. this is known behavior: fuck ncurses. instead,
	// we use mvwins_wch, which doesn't update the cursor position.
	// see http://lists.gnu.org/archive/html/bug-ncurses/2007-09/msg00001.html
	mvwadd_wch(w, 0, 0, &bchr[0]);
	whline(w, ACS_HLINE, cols - 2);
	mvwins_wch(w, 0, cols - 1, &bchr[1]);
	if(rows > 1){
		mvwvline(w, 1, cols - 1, ACS_VLINE, rows - 1);
		mvwvline(w, 1, 0, ACS_VLINE, rows - 1);
	}
	mvwadd_wch(w, rows - 1, 0, &bchr[2]);
	whline(w, ACS_HLINE, cols - 2);
	mvwins_wch(w, rows - 1, cols - 1, &bchr[3]);
	return 0;
}
