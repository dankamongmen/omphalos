#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <net/if.h>
#include <ncurses.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>
#include <gnu/libc-version.h>

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98-pre"	// FIXME

enum {
	BORDER_COLOR = 1,
	HEADING_COLOR = 2,
	DBORDER_COLOR = 3,
	DHEADING_COLOR = 4,
	UBORDER_COLOR = 5,
	UHEADING_COLOR = 6,
};

static WINDOW *pad;
static pthread_t inputtid;
static struct utsname sysuts;
static unsigned current_iface,count_interface;
static const char *glibc_version,*glibc_release;

// FIXME do stuff here, proof of concept skeleton currently
static void *
ncurses_input_thread(void *nil){
	int ch;

	if(!nil){
		while((ch = getch()) != 'q' && ch != 'Q'){
		switch(ch){
			case KEY_UP: case 'k':
				if(current_iface > 0){
					--current_iface;
				}
				break;
			case KEY_DOWN: case 'j':
				if(current_iface < count_interface){
					++current_iface;
				}
				break;
			default:
				break;
		}
		}
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
	if(wprintw(w,"%s %s on %s %s (libc %s-%s)",name,ver,sysuts.sysname,
				sysuts.release,glibc_version,glibc_release) < 0){
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
	if(prefresh(w,0,0,0,0,LINES,COLS)){
		return -1;
	}
	if(wcolor_set(w,0,NULL) != OK){
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
mandatory_cleanup(WINDOW *w,WINDOW *pad){
	int ret = 0;

	if(delwin(pad) != OK){
		ret = -1;
	}
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
ncurses_setup(WINDOW **mainwin){
	WINDOW *w = NULL;

	if((*mainwin = initscr()) == NULL){
		fprintf(stderr,"Couldn't initialize ncurses\n");
		goto err;
	}
	if((w = newpad(LINES,COLS)) == NULL){
		fprintf(stderr,"Couldn't initialize main pad\n");
		goto err;
	}
	if(cbreak() != OK){
		fprintf(stderr,"Couldn't disable input buffering\n");
		goto err;
	}
	if(noecho() != OK){
		fprintf(stderr,"Couldn't disable input echoing\n");
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
	if(init_pair(BORDER_COLOR,COLOR_GREEN,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(HEADING_COLOR,COLOR_YELLOW,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(DBORDER_COLOR,COLOR_YELLOW,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(DHEADING_COLOR,COLOR_WHITE,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(UBORDER_COLOR,COLOR_YELLOW,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(UHEADING_COLOR,COLOR_GREEN,-1) != OK){
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
	mandatory_cleanup(*mainwin,w);
	*mainwin = NULL;
	return NULL;
}

// Bind one of these state structures to each interface
typedef struct iface_state {
	int scrline;
	WINDOW *subpad;
	uintmax_t pkts;
	unsigned iface_position;
} iface_state;

#define PAD_LINES 4
#define PAD_COLS (COLS - START_COL * 2)
#define START_LINE 2
#define START_COL 2

static int
print_iface_state(const interface *i __attribute__ ((unused)),const iface_state *is){
	if(mvwprintw(is->subpad,1,1,"pkts: %ju",is->pkts) != OK){
		return -1;
	}
	if(prefresh(is->subpad,0,0,is->scrline,START_COL,is->scrline + PAD_LINES,START_COL + PAD_COLS) != OK){
		return -1;
	}
	return 0;
}

static void
packet_callback(const interface *i,void *unsafe){
	iface_state *is = unsafe;

	if(unsafe){
		++is->pkts;
		print_iface_state(i,is);
	}
}

static WINDOW *
iface_box(WINDOW *parent,unsigned line,const interface *i,const iface_state *is){
	int bcolor,hcolor;
	WINDOW *w;
	int attrs;

	// FIXME shouldn't have to know IFF_UP out here
	bcolor = (i->flags & IFF_UP) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = (i->flags & IFF_UP) ? UHEADING_COLOR : DHEADING_COLOR;
	attrs = ((is->iface_position == current_iface) ? A_REVERSE : 0) | A_BOLD;
	if((w = subpad(parent,PAD_LINES,PAD_COLS,line,START_COL)) == NULL){
		return NULL;
	}
	if(wattron(w,attrs | COLOR_PAIR(bcolor)) != OK){
		goto err;
	}
	if(box(w,0,0) != OK){
		goto err;
	}
	if(wattroff(w,A_REVERSE)){
		goto err;
	}
	if(mvwprintw(w,0,START_COL,"[") < 0){
		goto err;
	}
	if(wcolor_set(w,hcolor,NULL)){
		goto err;
	}
	if(waddstr(w,i->name) != OK){
		goto err;
	}
	if(strlen(i->drv.driver)){
		if(wprintw(w," (%s",i->drv.driver) != OK){
			goto err;
		}
		if(strlen(i->drv.version)){
			if(wprintw(w," %s",i->drv.version) != OK){
				goto err;
			}
		}
		if(strlen(i->drv.fw_version)){
			if(wprintw(w," fw %s",i->drv.fw_version) != OK){
				goto err;
			}
		}
		if(waddch(w,')') != OK){
			goto err;
		}
	}
	if(strlen(i->drv.bus_info)){
		if(wprintw(w," @ %s",i->drv.bus_info) != OK){
			goto err;
		}
	}
	if(wcolor_set(w,bcolor,NULL)){
		goto err;
	}
	if(wprintw(w,"]") < 0){
		goto err;
	}
	if(wattroff(w,attrs) != OK){
		goto err;
	}
	if(wcolor_set(w,0,NULL) != OK){
		goto err;
	}
	return w;

err:
	delwin(w);
	return NULL;
}

static void *
interface_callback(const interface *i,void *unsafe){
	iface_state *ret;

	if((ret = unsafe) == NULL){
		if( (ret = malloc(sizeof(iface_state))) ){
			ret->scrline = START_LINE + count_interface * (PAD_LINES + 1);
			ret->pkts = 0;
			if( (ret->subpad = iface_box(pad,ret->scrline,i,ret)) ){
				if(i->flags & IFF_UP){
					print_iface_state(i,ret);
				}
				ret->iface_position = ++count_interface;
				touchwin(pad);
				prefresh(pad,0,0,0,0,LINES,COLS);
				if(current_iface == 0){
					current_iface = 1;
				}
			}else{
				free(ret);
				ret = NULL;
			}
		}
	}
	// FIXME otherwise, redraw to reflect interface status change
	return ret;
}

static void
interface_removed_callback(const interface *i __attribute__ ((unused)),void *unsafe){
	iface_state *is;

	if( (is = unsafe) ){
		delwin(is->subpad);
		free(is);
	}
}

int main(int argc,char * const *argv){
	omphalos_ctx pctx;
	WINDOW *w;

	if(setlocale(LC_ALL,"") == NULL){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if(uname(&sysuts)){
		fprintf(stderr,"Coudln't get OS info (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	glibc_version = gnu_get_libc_version();
	glibc_release = gnu_get_libc_release();
	if((pad = ncurses_setup(&w)) == NULL){
		return EXIT_FAILURE;
	}
	if(omphalos_setup(argc,argv,&pctx)){
		return EXIT_FAILURE;
	}
	pctx.iface.packet_read = packet_callback;
	pctx.iface.iface_event = interface_callback;
	pctx.iface.iface_removed = interface_removed_callback;
	if(omphalos_init(&pctx)){
		goto err;
	}
	omphalos_cleanup(&pctx);
	if(mandatory_cleanup(w,pad)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;

err:
	mandatory_cleanup(w,pad);
	return EXIT_FAILURE;
}
