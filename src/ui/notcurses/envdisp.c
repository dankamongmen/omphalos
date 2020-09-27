#include <stdlib.h>
#include <string.h>
#include <ui/notcurses/util.h>
#include <ui/notcurses/envdisp.h>

#define ENVROWS 10
#define COLORSPERROW 32

static int
env_details(struct ncplane *hw, int rows){
	const int col = START_COL;
	const int row = 1;
	int z, srows, scols;

	//wattrset(hw,SUBDISPLAY_ATTR);
	ncplane_dim_yx(notcurses_stdplane(NC), &srows, &scols);
	if((z = rows) >= ENVROWS){
		z = ENVROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case 1:{
		ncplane_printf_yx(hw, row + z, col, "Geom: %dx%d", srows, scols);
		--z;
	}	/* intentional fallthrough */
	case 0:{
		const char *lang = getenv("LANG");
		const char *term = getenv("TERM");

		lang = lang ? lang : "Undefined";
		ncplane_printf_yx(hw, row + z, col, "LANG: %-21s TERM: %s", lang, term);
		--z;
		break;
	}default:{
		return -1;
	}
	}
	return 0;
}

int display_env_locked(struct ncplane *mainw, struct panel_state *ps){
	memset(ps, 0, sizeof(*ps));
	if(new_display_panel(mainw, ps, ENVROWS, 76, L"press 'e' to dismiss display")){
		goto err;
	}
	if(env_details(ps->n, ps->ysize)){
		goto err;
	}
	return 0;

err:
  ncplane_destroy(ps->n);
	memset(ps, 0, sizeof(*ps));
	return -1;
}
