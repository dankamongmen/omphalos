#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <ncurses.h>
#include <pthread.h>
#include <omphalos/omphalos.h>

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98pre"	// FIXME

enum {
	BORDER_COLOR = 1,
	HEADING_COLOR = 2,
};

static pthread_t inputtid;

// FIXME do stuff here, proof of concept skeleton currently
static void *
ncurses_input_thread(void *nil){
	int ch;

	if(!nil){
		while((ch = getch()) != 'q' && ch != 'Q');
		printf("DONE\n");
		raise(SIGINT);
	}
	pthread_exit(NULL);
}

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
	if(wattron(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
		return -1;
	}
	if(wprintw(w,"%s %s",name,ver) < 0){
		return -1;
	}
	if(wattroff(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
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
	if(pthread_create(&inputtid,NULL,ncurses_input_thread,NULL)){
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
	if(cbreak() != OK){
		fprintf(stderr,"Couldn't disable input buffering\n");
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
	if(init_pair(BORDER_COLOR,COLOR_GREEN,COLOR_BLACK) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(HEADING_COLOR,COLOR_YELLOW,COLOR_BLACK) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(curs_set(0) == ERR){
		fprintf(stderr,"Couldn't disable cursor\n");
		goto err;
	}
	if(draw_main_window(w,PROGNAME,VERSION)){
		fprintf(stderr,"Couldn't use ncurses\n");
		goto err;
	}
	return w;

err:
	mandatory_cleanup(w);
	return NULL;
}

static void
packet_callback(void){
	static uint64_t pkts = 0;

	// FIXME will need to move to per-interface window
	mvprintw(2,2,"pkts: %lu\n",++pkts);
}

static void
interface_callback(const struct interface *i){
	if(i == NULL){
		return;
	}
}

int main(int argc,char * const *argv){
	omphalos_ctx pctx;
	WINDOW *w;

	if(setlocale(LC_ALL,"") == NULL){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if((w = ncurses_setup()) == NULL){
		return EXIT_FAILURE;
	}
	if(omphalos_setup(argc,argv,&pctx)){
		return EXIT_FAILURE;
	}
	pctx.iface.packet_read = packet_callback;
	pctx.iface.iface_event = interface_callback;
	if(omphalos_init(&pctx)){
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
