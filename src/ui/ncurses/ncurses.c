#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
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

#define PAD_LINES 4
#define PAD_COLS (COLS - START_COL * 2)
#define START_LINE 2
#define START_COL 2

// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via ifacenum (for UI actions).
typedef struct iface_state {
	int ifacenum;			// iface number
	int scrline;			// line within the containing pad
	WINDOW *subpad;			// subpad
	struct iface_state *next,*prev;
} iface_state;

enum {
	BORDER_COLOR = 1,
	HEADING_COLOR = 2,
	DBORDER_COLOR = 3,
	DHEADING_COLOR = 4,
	UBORDER_COLOR = 5,
	UHEADING_COLOR = 6,
};

// FIXME granularize things, make packet handler iret-like
static pthread_mutex_t bfl = PTHREAD_MUTEX_INITIALIZER;

static WINDOW *pad;
static pthread_t inputtid;
static struct utsname sysuts;
static unsigned count_interface;
static iface_state *current_iface;
static const char *glibc_version,*glibc_release;

// Status bar at the bottom of the screen. Must be reallocated upon screen
// resize and allocated based on initial screen at startup. Don't shrink
// it; widening the window again should show the full message.
static char *statusmsg;
static int statuschars;	// True size, not necessarily what's available

#define ANSITERM_COLS 80

// Pass current number of columns
static int
setup_statusbar(int cols){
	if(cols < 0){
		return -1;
	}else if(cols < ANSITERM_COLS){
		cols = ANSITERM_COLS;
	}
	if(statuschars <= cols){
		char *sm;

		if((sm = realloc(statusmsg,cols + 1)) == NULL){
			return -1;
		}
		statuschars = cols + 1;
		if(statusmsg == NULL){
			sm[0] = '\0';
		}
		statusmsg = sm;
	}
	return 0;
}

static int
iface_box(WINDOW *w,const interface *i,const iface_state *is){
	int bcolor,hcolor;
	size_t buslen;
	int attrs;

	// FIXME shouldn't have to know IFF_UP out here
	bcolor = (i->flags & IFF_UP) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = (i->flags & IFF_UP) ? UHEADING_COLOR : DHEADING_COLOR;
	attrs = ((is == current_iface) ? A_REVERSE : 0) | A_BOLD;
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
	if(waddstr(w,i->name) == ERR){
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
	if(wcolor_set(w,bcolor,NULL)){
		goto err;
	}
	if(wprintw(w,"]") < 0){
		goto err;
	}
	if(wattron(w,attrs | COLOR_PAIR(hcolor))){
		goto err;
	}
	if(wattroff(w,A_BOLD) != OK){
		goto err;
	}
	if( (buslen = strlen(i->drv.bus_info)) ){
		if(mvwprintw(w,PAD_LINES - 1,COLS - (buslen + 3 + START_COL),"%s",i->drv.bus_info) != OK){
			goto err;
		}
	}
	if(wcolor_set(w,0,NULL) != OK){
		goto err;
	}
	if(wattroff(w,attrs) != OK){
		goto err;
	}
	return 0;

err:
	return -1;
}

static int
draw_main_window(WINDOW *w,const char *name,const char *ver){
	int rows,cols;

	getmaxyx(w,rows,cols);
	if(setup_statusbar(cols)){
		return -1;
	}
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
	if(wattron(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
		return -1;
	}
	if(mvprintw(rows - 1,START_COL,"%s",statusmsg) != OK){
		return -1;
	}
	if(wattroff(w,A_BOLD | COLOR_PAIR(BORDER_COLOR)) != OK){
		return -1;
	}
	if(prefresh(w,0,0,0,0,LINES,COLS)){
		return -1;
	}
	if(wcolor_set(w,0,NULL) != OK){
		return -1;
	}
	return 0;
}

static int
wvstatus_locked(WINDOW *w,const char *fmt,va_list va){
	if(fmt == NULL){
		statusmsg[0] = '\0';
	}else{
		vsnprintf(statusmsg,statuschars,fmt,va);
	}
	return draw_main_window(w,PROGNAME,VERSION);
}

// NULL fmt clears the status bar
static int
wvstatus(WINDOW *w,const char *fmt,va_list va){
	int ret;

	pthread_mutex_lock(&bfl);
	ret = wvstatus_locked(w,fmt,va);
	pthread_mutex_unlock(&bfl);
	if(ret != OK){
		abort();
	}
	return ret;
}

// NULL fmt clears the status bar
static int
wstatus(WINDOW *w,const char *fmt,...){
	va_list va;
	int ret;

	va_start(va,fmt);
	ret = wvstatus(w,fmt,va);
	va_end(va);
	return ret;
}

// FIXME do stuff here, proof of concept skeleton currently
static void *
ncurses_input_thread(void *nil __attribute__ ((unused))){
	int ch;

	while((ch = getch()) != 'q' && ch != 'Q'){
	switch(ch){
		case KEY_UP: case 'k':
			pthread_mutex_lock(&bfl);
			if(current_iface && current_iface->prev){
				const iface_state *is = current_iface;
				interface *i = iface_by_idx(is->ifacenum);

				current_iface = current_iface->prev;
				iface_box(is->subpad,i,is);
				is = current_iface;
				i = iface_by_idx(is->ifacenum);
				iface_box(is->subpad,i,is);
				draw_main_window(pad,PROGNAME,VERSION);
			}
			pthread_mutex_unlock(&bfl);
			break;
		case KEY_DOWN: case 'j':
			pthread_mutex_lock(&bfl);
			if(current_iface && current_iface->next){
				const iface_state *is = current_iface;
				interface *i = iface_by_idx(is->ifacenum);

				current_iface = current_iface->next;
				iface_box(is->subpad,i,is);
				is = current_iface;
				i = iface_by_idx(is->ifacenum);
				iface_box(is->subpad,i,is);
				draw_main_window(pad,PROGNAME,VERSION);
			}
			pthread_mutex_unlock(&bfl);
			break;
		case 'h':
			wstatus(pad,"there is no help here"); // FIXME
			break;
		default:
			if(isprint(ch)){
				wstatus(pad,"unknown command '%c' ('h' for help)",ch);
			}else{
				wstatus(pad,"unknown scancode '%d' ('h' for help)",ch);
			}
			break;
	}
	}
	wstatus(pad,"%s","shutting down");
	// we can't use raise() here, as that sends the signal only
	// to ourselves, and we have it masked.
	kill(getpid(),SIGINT);
	pthread_exit(NULL);
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
	if((w = newpad(LINES,COLS)) == NULL){
		fprintf(stderr,"Couldn't initialize main pad\n");
		goto err;
	}
	if(cbreak() != OK){
		fprintf(stderr,"Couldn't disable input buffering\n");
		goto err;
	}
	if(keypad(stdscr,TRUE) != OK){
		fprintf(stderr,"Warning: couldn't enable keypad input\n");
	}
	if(init_pair(BORDER_COLOR,COLOR_GREEN,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(HEADING_COLOR,COLOR_YELLOW,-1) != OK){
		fprintf(stderr,"Couldn't initialize ncurses colorpair\n");
		goto err;
	}
	if(init_pair(DBORDER_COLOR,COLOR_WHITE,-1) != OK){
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
	if(setup_statusbar(COLS)){
		fprintf(stderr,"Couldn't setup status bar\n");
		goto err;
	}
	if(draw_main_window(w,PROGNAME,VERSION)){
		fprintf(stderr,"Couldn't use ncurses\n");
		goto err;
	}
	if(pthread_create(&inputtid,NULL,ncurses_input_thread,NULL)){
		fprintf(stderr,"Couldn't create UI thread\n");
		goto err;
	}
	// FIXME install SIGWINCH() handler...
	return w;

err:
	mandatory_cleanup(*mainwin,w);
	*mainwin = NULL;
	fprintf(stderr,"Error while preparing ncurses\n");
	return NULL;
}

static int
print_iface_state(const interface *i __attribute__ ((unused)),const iface_state *is){
	if(mvwprintw(is->subpad,1,1,"pkts: %ju",i->frames) != OK){
		return -1;
	}
	if(prefresh(is->subpad,0,0,is->scrline,START_COL,is->scrline + PAD_LINES,START_COL + PAD_COLS) != OK){
		return -1;
	}
	return 0;
}

static inline void
packet_cb_locked(const interface *i,iface_state *is){
	if(is){
		print_iface_state(i,is);
	}
}

static void
packet_callback(const interface *i __attribute__ ((unused)),void *unsafe){
	pthread_mutex_lock(&bfl);
	packet_cb_locked(i,unsafe);
	pthread_mutex_unlock(&bfl);
}

static inline void *
interface_cb_locked(const interface *i,int inum,iface_state *ret){
	if(ret == NULL){
		if( (ret = malloc(sizeof(iface_state))) ){
			ret->scrline = START_LINE + count_interface * (PAD_LINES + 1);
			ret->ifacenum = inum;
			if((ret->prev = current_iface) == NULL){
				current_iface = ret;
				ret->next = NULL;
			}else{
				while(ret->prev->next){
					ret->prev = ret->prev->next;
				}
				ret->prev->next = ret;
			}
			if( (ret->subpad = subpad(pad,PAD_LINES,PAD_COLS,ret->scrline,START_COL)) ){
				++count_interface;
			}else{
				if(current_iface == ret){
					current_iface = NULL;
				}else{
					ret->prev->next = NULL;
				}
				free(ret);
				ret = NULL;
			}
		}
	}
	if(ret){
		iface_box(ret->subpad,i,ret);
		if(i->flags & IFF_UP){
			print_iface_state(i,ret);
		}
		touchwin(pad);
		prefresh(pad,0,0,0,0,LINES,COLS);
	}
	return ret;
}

static void *
interface_callback(const interface *i,int inum,void *unsafe){
	void *r;

	pthread_mutex_lock(&bfl);
	r = interface_cb_locked(i,inum,unsafe);
	pthread_mutex_unlock(&bfl);
	return r;
}

static inline void
interface_removed_locked(iface_state *is){
	if(is){
		delwin(is->subpad);
		is->next->prev = is->prev;
		is->prev->next = is->next;
		if(is == current_iface){
			current_iface = is->prev;
		}
		free(is);
	}
}

static void
interface_removed_callback(const interface *i __attribute__ ((unused)),void *unsafe){
	pthread_mutex_lock(&bfl);
	interface_removed_locked(unsafe);
	pthread_mutex_unlock(&bfl);
}

static void
diag_callback(const char *fmt,...){
	va_list va;

	va_start(va,fmt);
	wvstatus(pad,fmt,va);
	va_end(va);
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
	if(omphalos_setup(argc,argv,&pctx)){
		return EXIT_FAILURE;
	}
	if((pad = ncurses_setup(&w)) == NULL){
		return EXIT_FAILURE;
	}
	pctx.iface.packet_read = packet_callback;
	pctx.iface.iface_event = interface_callback;
	pctx.iface.iface_removed = interface_removed_callback;
	pctx.iface.diagnostic = diag_callback;
	if(omphalos_init(&pctx)){
		int err = errno;

		mandatory_cleanup(w,pad);
		fprintf(stderr,"Error in omphalos_init() (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	omphalos_cleanup(&pctx);
	if(mandatory_cleanup(w,pad)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
