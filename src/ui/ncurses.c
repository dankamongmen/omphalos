#include <errno.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <ncurses.h>
#include <omphalos/omphalos.h>

#define PROGNAME "omphalos" // FIXME

static int
draw_main_window(WINDOW *w,const char *name){
	if(wprintw(w,"%s\n",name) < 0){
		return -1;
	}
	if(box(w,0,0) != OK){
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
mandatory_cleanup(void){
	if(endwin() != OK){
		fprintf(stderr,"Couldn't cleanup ncurses\n");
		return -1;
	}
	return 0;
}

int main(int argc,char * const *argv){
	WINDOW *w;

	if(setlocale(LC_ALL,"") == NULL){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if((w = initscr()) == NULL){
		fprintf(stderr,"Couldn't initialize ncurses\n");
		goto err;
	}
	if(draw_main_window(w,PROGNAME)){
		fprintf(stderr,"Couldn't use ncurses\n");
		goto err;
	}
	if(omphalos_init(argc,argv)){
		goto err;
	}
	omphalos_cleanup();
	if(mandatory_cleanup()){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;

err:
	mandatory_cleanup();
	return EXIT_FAILURE;
}
