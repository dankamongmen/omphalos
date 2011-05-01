#include <errno.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <ncurses.h>
#include <omphalos/omphalos.h>

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98pre"	// FIXME

enum {
	BORDER_COLOR = 1,
	HEADING_COLOR = 2,
};

static int
draw_main_window(WINDOW *w,const char *name,const char *ver){
	if(wcolor_set(w,BORDER_COLOR,NULL) != OK){
		return -1;
	}
	if(box(w,0,0) != OK){
		return -1;
	}
	if(mvwprintw(w,0,2,"[") < 0){
		return -1;
	}
	if(wcolor_set(w,HEADING_COLOR,NULL) != OK){
		return -1;
	}
	if(wattron(w,A_BOLD) != OK){
		return -1;
	}
	if(wprintw(w,"%s %s",name,ver) < 0){
		return -1;
	}
	if(wattroff(w,A_BOLD) != OK){
		return -1;
	}
	if(wcolor_set(w,BORDER_COLOR,NULL) != OK){
		return -1;
	}
	if(wprintw(w,"]") < 0){
		return -1;
	}
	if(wrefresh(w)){
		return -1;
	}
	return 0;
}

// Cleanup which ought be performed even if we had a failure elsewhere, or
// indeed never started.
static int
mandatory_cleanup(WINDOW *w){
	int ret = 0;

	if(delwin(w) != OK){
		ret = -1;
	}
	if(endwin() != OK){
		ret = -1;
	}
	if(ret){
		fprintf(stderr,"Couldn't cleanup ncurses\n");
	}
	return ret;
}

static WINDOW *
ncurses_setup(void){
	WINDOW *w;

	if((w = initscr()) == NULL){
		fprintf(stderr,"Couldn't initialize ncurses\n");
		goto err;
	}
	if(start_color() != OK){
		fprintf(stderr,"Couldn't initialize ncurses color\n");
		goto err;
	}
	if(use_default_colors()){
		fprintf(stderr,"Couldn't initialize ncurses colordefs\n");
		goto err;
	}
	if(init_pair(BORDER_COLOR,COLOR_GREEN,COLOR_BLACK)){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(HEADING_COLOR,COLOR_YELLOW,COLOR_BLACK)){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	return w;

err:
	mandatory_cleanup(w);
	return NULL;
}

int main(int argc,char * const *argv){
	WINDOW *w;

	if(setlocale(LC_ALL,"") == NULL){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if((w = ncurses_setup()) == NULL){
		return EXIT_FAILURE;
	}
	if(draw_main_window(w,PROGNAME,VERSION)){
		fprintf(stderr,"Couldn't use ncurses\n");
		goto err;
	}
	if(omphalos_init(argc,argv)){
		goto err;
	}
	omphalos_cleanup();
	if(mandatory_cleanup(w)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;

err:
	mandatory_cleanup(w);
	return EXIT_FAILURE;
}
