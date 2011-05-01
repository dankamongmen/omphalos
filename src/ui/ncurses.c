#include <stdlib.h>
#include <ncurses.h>
#include <omphalos/omphalos.h>

int main(int argc,char * const *argv){
	if(omphalos_init(argc,argv)){
		return EXIT_FAILURE;
	}
	omphalos_cleanup();
	return EXIT_SUCCESS;
}
