#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <pthread.h>
#include <langinfo.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <linux/if.h>

// The wireless extensions headers are not so fantastic. This workaround comes
// to us courtesy of Jean II in iwlib.h. Ugh.
#ifndef __user
#define __user
#endif
#include <asm/types.h>
#include <wireless.h>

#include <sys/utsname.h>
#include <linux/version.h>
#include <ncursesw/panel.h>
#include <linux/rtnetlink.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/core.h>
#include <omphalos/timing.h>
#include <ncursesw/ncurses.h>
#include <omphalos/hwaddrs.h>
#include <ui/ncurses/iface.h>
#include <gnu/libc-version.h>
#include <omphalos/ethtool.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define ERREXIT endwin() ; fprintf(stderr,"ncurses failure|%s|%d\n",__func__,__LINE__); abort() ; goto err

#define PANEL_STATE_INITIALIZER { .p = NULL, .ysize = -1, }

static struct panel_state help = PANEL_STATE_INITIALIZER;
static struct panel_state details = PANEL_STATE_INITIALIZER;
static struct panel_state network = PANEL_STATE_INITIALIZER;

// Add ((format (printf))) attributes to ncurses functions, which sadly
// lack them (at least as of Debian's 5.9-1).
extern int wprintw(WINDOW *,const char *,...) __attribute__ ((format (printf,2,3)));
extern int mvwprintw(WINDOW *,int,int,const char *,...) __attribute__ ((format (printf,4,5)));

static struct panel_state *active;

// FIXME granularize things, make packet handler iret-like
static pthread_mutex_t bfl = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static pthread_t inputtid;

// Old host versioning display info
static const char *glibc_version,*glibc_release; // Currently unused
static struct utsname sysuts; // Currently unused

static inline void
lock_ncurses(void){
	assert(pthread_mutex_lock(&bfl) == 0);
	check_consistency();
}

static inline void
unlock_ncurses(void){
	screen_update();
	check_consistency();
	assert(pthread_mutex_unlock(&bfl) == 0);
}

// NULL fmt clears the status bar. wvstatus is an unlocked entry point, and
// thus calls screen_update() on exit.
static int
wvstatus(WINDOW *w,const wchar_t *fmt,va_list va){
	int ret;

	lock_ncurses();
	ret = wvstatus_locked(w,fmt,va);
	screen_update();
	unlock_ncurses();
	return ret;
}

// NULL fmt clears the status bar. wstatus is an unlocked entry point, and thus
// calls screen_update() on exit.
static int
wstatus(WINDOW *w,const wchar_t *fmt,...){
	va_list va;
	int ret;

	va_start(va,fmt);
	ret = wvstatus(w,fmt,va); // calls screen_update()
	va_end(va);
	return ret;
}

static const wchar_t *helps[] = {
	/*L"'n': network configuration",
	L"       configure addresses, routes, bridges, and wireless",
	L"'a': attack configuration",
	L"       configure quenching, assassinations, and deauth/disassoc",
	L"'J': hijack configuration",
	L"       configure fake APs, rogue DHCP/DNS, and ARP MitM",
	L"'D': defense configuration",
	L"       define authoritative configurations to enforce",
	L"'S': secrets database",
	L"       export pilfered passwords, cookies, and identifying data",
	L"'c': crypto configuration",
	L"       configure algorithm stepdown, WEP/WPA cracking, SSL MitM", */
	L"'C': configuration",
	L"'⏎Enter': browse interface    '␛Esc': leave interface browser",
	L"'k'/'↑': previous interface   'j'/'↓': next interface",
	L"'-'/'←': collapse interface   '+'/'→': expand interface",
	L"'m': change device MAC        'u': change device MTU",
	L"'r': reset interface's stats  'R': reset all interfaces' stats",
	L"'d': bring down device        'p': toggle promiscuity",
	L"'s': toggle sniffing, bringing up interface if down",
	L"'v': view interface details   'n': view networking details",
	L"'h': toggle this help display",
	L"'q': quit                     ctrl+'L': redraw the screen",
	NULL
};

// FIXME need to support scrolling through the list
static int
helpstrs(WINDOW *hw,int row,int rows){
	const wchar_t *hs;
	int z;

	for(z = 0 ; (hs = helps[z]) && z < rows ; ++z){
		if(mvwaddwstr(hw,row + z,1,hs) == ERR){
			return ERR;
		}
	}
	return OK;
}

static size_t
max_helpstr_len(const wchar_t **helps){
	size_t max = 0;

	while(*helps){
		if(wcslen(*helps) > max){
			max = wcslen(*helps);
		}
		++helps;
	}
	return max;
}

static int
display_help_locked(WINDOW *mainw,struct panel_state *ps){
	static const int helprows = sizeof(helps) / sizeof(*helps) - 1; // NULL != row
	const int helpcols = max_helpstr_len(helps) + 4; // spacing + borders

	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,helprows,helpcols,L"press 'h' to dismiss help")){
		ERREXIT;
	}
	if(helpstrs(panel_window(ps->p),1,ps->ysize) != OK){
		ERREXIT;
	}
	return 0;

err:
	if(ps->p){
		WINDOW *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}

static void
configure_prefs(WINDOW *w){
	unimplemented(w);
}

static void
change_mac(WINDOW *w){
	unimplemented(w);
}

static void
change_mtu(WINDOW *w){
	unimplemented(w);
}

static void
resize_screen_locked(WINDOW *w){
	/*int rows,cols;

	getmaxyx(w,rows,cols);*/
	draw_main_window(w);
}

// Completely redraw the screen, for instance after a corruption (see wrefresh
// man pageL: "If the argument to wrefresh is curscr, the screen is immediately
// cleared and repainted from scratch."
static void
redraw_screen_locked(void){
	wrefresh(curscr);
}

struct ncurses_input_marshal {
	WINDOW *w;
	const omphalos_iface *octx;
};

static void *
ncurses_input_thread(void *unsafe_marsh){
	struct ncurses_input_marshal *nim = unsafe_marsh;
	const omphalos_iface *octx = nim->octx;
	WINDOW *w = nim->w;
	int ch;

	active = NULL; // No subpanels initially
	memset(&help,0,sizeof(help));
	memset(&details,0,sizeof(details));
	while((ch = getch()) != 'q' && ch != 'Q'){
	switch(ch){
		case KEY_UP: case 'k':
			lock_ncurses();
				use_prev_iface_locked(w,&details);
			unlock_ncurses();
			break;
		case KEY_DOWN: case 'j':
			lock_ncurses();
				use_next_iface_locked(w,&details);
			unlock_ncurses();
			break;
		case KEY_RESIZE:
			lock_ncurses();{
				resize_screen_locked(w);
			}unlock_ncurses();
			break;
		case 12: // Ctrl-L FIXME
			lock_ncurses();{
				redraw_screen_locked();
			}unlock_ncurses();
			break;
		case 13: // Enter FIXME
			lock_ncurses();{
				select_iface_locked();
			}unlock_ncurses();
			break;
		case 27: // Escape FIXME
			lock_ncurses();{
				deselect_iface_locked();
			}unlock_ncurses();
			break;
		case 'C':
			lock_ncurses();
				configure_prefs(w);
			unlock_ncurses();
			break;
		case 'R':
			lock_ncurses();
				reset_all_interface_stats(w);
			unlock_ncurses();
			break;
		case 'r':
			lock_ncurses();
				reset_current_interface_stats(w);
			unlock_ncurses();
			break;
		case 'p':
			lock_ncurses();
				toggle_promisc_locked(octx,w);
			unlock_ncurses();
			break;
		case 'd':
			lock_ncurses();
				down_interface_locked(octx,w);
			unlock_ncurses();
			break;
		case 's':
			lock_ncurses();
				sniff_interface_locked(octx,w);
			unlock_ncurses();
			break;
		case 'u':
			lock_ncurses();
				change_mtu(w);
			unlock_ncurses();
			break;
		case '+':
		case KEY_RIGHT:
			lock_ncurses();
				expand_iface_locked(&details);
			unlock_ncurses();
			break;
		case '-':
		case KEY_LEFT:
			lock_ncurses();
				collapse_iface_locked(&details);
			unlock_ncurses();
			break;
		case 'm':
			lock_ncurses();
				change_mac(w);
			unlock_ncurses();
			break;
		case 'v':{
			lock_ncurses();
				if(details.p){
					hide_panel_locked(&details);
					active = NULL;
				}else{
					hide_panel_locked(active);
					active = (display_details_locked(w,&details) == OK)
						? &details : NULL;
				}
			unlock_ncurses();
			break;
		}case 'n':{
			lock_ncurses();
				if(network.p){
					hide_panel_locked(&network);
					active = NULL;
				}else{
					hide_panel_locked(active);
					active = (display_network_locked(w,&network) == OK)
						? &details : NULL;
				}
			unlock_ncurses();
			break;
		}case 'h':{
			lock_ncurses();
				if(help.p){
					hide_panel_locked(&help);
					active = NULL;
				}else{
					hide_panel_locked(active);
					active = (display_help_locked(w,&help) == OK)
						? &help : NULL;
				}
			unlock_ncurses();
			break;
		}default:{
			const char *hstr = !help.p ? " ('h' for help)" : "";
			// wstatus() locks/unlocks, and calls screen_update()
			if(isprint(ch)){
				wstatus(w,L"unknown command '%c'%s",ch,hstr);
			}else{
				wstatus(w,L"unknown scancode %d%s",ch,hstr);
			}
			break;
		}
	}
	}
	wstatus(w,L"%s","shutting down");
	// we can't use raise() here, as that sends the signal only
	// to ourselves, and we have it masked.
	kill(getpid(),SIGINT);
	pthread_exit(NULL);
}

// Cleanup which ought be performed even if we had a failure elsewhere, or
// indeed never started.
static int
mandatory_cleanup(WINDOW **w){
	int ret = 0;

	pthread_mutex_lock(&bfl);
	if(*w){
		if(delwin(*w) != OK){
			ret = -1;
		}
		*w = NULL;
	}
	if(stdscr){
		if(delwin(stdscr) != OK){
			ret = -2;
		}
		stdscr = NULL;
	}
	if(endwin() != OK){
		ret = -3;
	}
	pthread_mutex_unlock(&bfl);
	switch(ret){
	case -3: fprintf(stderr,"Couldn't end main window\n"); break;
	case -2: fprintf(stderr,"Couldn't delete main window\n"); break;
	case -1: fprintf(stderr,"Couldn't delete main pad\n"); break;
	case 0: break;
	default: fprintf(stderr,"Couldn't cleanup ncurses\n"); break;
	}
	return ret;
}

static WINDOW *
ncurses_setup(const omphalos_iface *octx){
	struct ncurses_input_marshal *nim;
	const wchar_t *errstr = NULL;
	WINDOW *w = NULL;

	fwprintf(stderr,L"Entering ncurses mode...\n");
	if(initscr() == NULL){
		fwprintf(stderr,L"Couldn't initialize ncurses\n");
		return NULL;
	}
	if(cbreak() != OK){
		errstr = L"Couldn't disable input buffering\n";
		goto err;
	}
	if(noecho() != OK){
		errstr = L"Couldn't disable input echoing\n";
		goto err;
	}
	if(intrflush(stdscr,TRUE) != OK){
		errstr = L"Couldn't set flush-on-interrupt\n";
		goto err;
	}
	if(scrollok(stdscr,FALSE) != OK){
		errstr = L"Couldn't disable scrolling\n";
		goto err;
	}
	if(nonl() != OK){
		errstr = L"Couldn't disable nl translation\n";
		goto err;
	}
	if(start_color() != OK){
		errstr = L"Couldn't initialize ncurses color\n";
		goto err;
	}
	if(use_default_colors()){
		errstr = L"Couldn't initialize ncurses colordefs\n";
		goto err;
	}
	w = stdscr;
	keypad(stdscr,TRUE);
	if(nodelay(stdscr,FALSE) != OK){
		errstr = L"Couldn't set blocking input\n";
		goto err;
	}
	if(preserve_colors() != OK){
		errstr = L"Couldn't preserve initial colors\n";
		goto err;
	}
	if(init_pair(BORDER_COLOR,COLOR_GREEN,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(HEADER_COLOR,COLOR_BLUE,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(FOOTER_COLOR,COLOR_YELLOW,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(DBORDER_COLOR,COLOR_WHITE,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(DHEADING_COLOR,COLOR_WHITE,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(UBORDER_COLOR,COLOR_YELLOW,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(UHEADING_COLOR,COLOR_GREEN,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(PBORDER_COLOR,COLOR_CYAN,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(PHEADING_COLOR,COLOR_RED,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(BULKTEXT_COLOR,COLOR_WHITE,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(IFACE_COLOR,COLOR_WHITE,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	assert(init_pair(LCAST_COLOR,COLOR_CYAN,-1) == OK);
	assert(init_pair(UCAST_COLOR,COLOR_CYAN,-1) == OK);
	assert(init_pair(MCAST_COLOR,COLOR_BLUE,-1) == OK);
	assert(init_pair(BCAST_COLOR,COLOR_BLUE,-1) == OK);
	if(init_pair(ROUTER_COLOR,COLOR_YELLOW,-1) != OK){
		errstr = L"Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(setup_extended_colors() != OK){
		errstr = L"Couldn't initialize extended colors\n";
		assert(init_pair(LCAST_L3_COLOR,COLOR_CYAN,-1) == OK);
		assert(init_pair(UCAST_L3_COLOR,COLOR_CYAN,-1) == OK);
		assert(init_pair(MCAST_L3_COLOR,COLOR_BLUE,-1) == OK);
		assert(init_pair(BCAST_L3_COLOR,COLOR_BLUE,-1) == OK);
	}else{
		assert(init_pair(LCAST_L3_COLOR,COLOR_CYAN_75,-1) == OK);
		assert(init_pair(UCAST_L3_COLOR,COLOR_CYAN_75,-1) == OK);
		assert(init_pair(MCAST_L3_COLOR,COLOR_BLUE_75,-1) == OK);
		assert(init_pair(BCAST_L3_COLOR,COLOR_BLUE_75,-1) == OK);
	}
	if(curs_set(0) == ERR){
		errstr = L"Couldn't disable cursor\n";
		goto err;
	}
	if(setup_statusbar(COLS)){
		errstr = L"Couldn't setup status bar\n";
		goto err;
	}
	if(draw_main_window(w)){
		errstr = L"Couldn't use ncurses\n";
		goto err;
	}
	if((nim = malloc(sizeof(*nim))) == NULL){
		goto err;
	}
	nim->octx = octx;
	nim->w = w;
	// Panels aren't yet being used, so we need call refresh() to
	// paint the main window.
	refresh();
	if(pthread_create(&inputtid,NULL,ncurses_input_thread,nim)){
		errstr = L"Couldn't create UI thread\n";
		free(nim);
		goto err;
	}
	// FIXME install SIGWINCH() handler...?
	return w;

err:
	mandatory_cleanup(&w);
	fwprintf(stderr,L"%ls",errstr);
	return NULL;
}

static void
packet_callback(omphalos_packet *op){
	pthread_mutex_lock(&bfl); // don't always want screen_update()
	if(packet_cb_locked(op->i,op,&details)){
		screen_update();
	}
	pthread_mutex_unlock(&bfl);
}

static void *
interface_callback(interface *i,void *unsafe){
	void *r;

	lock_ncurses();
		r = interface_cb_locked(i,unsafe,&details);
	unlock_ncurses();
	return r;
}

static void *
wireless_callback(interface *i,unsigned wcmd __attribute__ ((unused)),void *unsafe){
	void *r;

	lock_ncurses();
		r = interface_cb_locked(i,unsafe,&details);
	unlock_ncurses();
	return r;
}

static void *
host_callback(const interface *i,struct l2host *l2,struct l3host *l3){
	void *ret;

	pthread_mutex_lock(&bfl);
	if( (ret = host_callback_locked(i,l2,l3,&details)) ){
		screen_update();
	}
	pthread_mutex_unlock(&bfl);
	return ret;
}

static void *
neighbor_callback(const interface *i,struct l2host *l2){
	void *ret;

	pthread_mutex_lock(&bfl);
	if( (ret = neighbor_callback_locked(i,l2,&details)) ){
		screen_update();
	}
	pthread_mutex_unlock(&bfl);
	return ret;
}

static void
interface_removed_callback(const interface *i __attribute__ ((unused)),void *unsafe){
	lock_ncurses();
		interface_removed_locked(unsafe,details.p ? &active : NULL);
	unlock_ncurses();
}

static void
diag_callback(const wchar_t *fmt,...){
	va_list va;

	va_start(va,fmt);
	wvstatus(stdscr,fmt,va);
	va_end(va);
}

int main(int argc,char * const *argv){
	const char *codeset;
	omphalos_ctx pctx;

	if(setlocale(LC_ALL,"") == NULL || ((codeset = nl_langinfo(CODESET)) == NULL)){
		fprintf(stderr,"Couldn't initialize locale (%s?)\n",strerror(errno));
		return EXIT_FAILURE;
	}
	if(strcmp(codeset,"UTF-8")){
		fprintf(stderr,"Only UTF-8 is supported; got %s\n",codeset);
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
	pctx.iface.packet_read = packet_callback;
	pctx.iface.iface_event = interface_callback;
	pctx.iface.iface_removed = interface_removed_callback;
	pctx.iface.diagnostic = diag_callback;
	pctx.iface.wireless_event = wireless_callback;
	pctx.iface.neigh_event = neighbor_callback;
	pctx.iface.host_event = host_callback;
	if(ncurses_setup(&pctx.iface) == NULL){
		return EXIT_FAILURE;
	}
	if(omphalos_init(&pctx)){
		int err = errno;

		mandatory_cleanup(&stdscr);
		fprintf(stderr,"Error in omphalos_init() (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	lock_ncurses();
	fade(1);
	restore_colors();
	unlock_ncurses();
	omphalos_cleanup(&pctx);
	if(mandatory_cleanup(&stdscr)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
