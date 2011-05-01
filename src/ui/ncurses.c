#include <errno.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <ncurses.h>
#include <omphalos/omphalos.h>

int main(int argc,char * const *argv){
	WINDOW *w;

	if(setlocale(LC_ALL,"") == NULL){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if((w = initscr()) == NULL){
		fprintf(stderr,"Couldn't initialize ncurses\n");
		return EXIT_FAILURE;
	}
	refresh();
	if(omphalos_init(argc,argv)){
		return EXIT_FAILURE;
	}
	omphalos_cleanup();
	if(endwin() != OK){
		fprintf(stderr,"Couldn't cleanup ncurses\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
