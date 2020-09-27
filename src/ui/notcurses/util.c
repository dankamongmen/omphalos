#include <assert.h>
#include <string.h>
#include <ui/notcurses/util.h>

int bevel(struct ncplane *w){
	int rows, cols;
	ncplane_dim_yx(w, &rows, &cols);
	assert(rows > 0 && cols > 0);
  ncplane_cursor_move_yx(w, 0, 0);
  ncplane_double_box(w, NCSTYLE_NONE, ncplane_channels(w), cols - 2, rows - 1, 0);
	return 0;
}
