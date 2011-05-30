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
#include <pthread.h>
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
#include <linux/nl80211.h>
#include <ncursesw/panel.h>
#include <ncursesw/ncurses.h>
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
	int sniffing;			// do we want to sniff?
	WINDOW *subwin;			// subwin
	PANEL *panel;			// panel
	const char *typestr;		// looked up using iface->arptype
	struct iface_state *next,*prev;
} iface_state;

enum {
	BORDER_COLOR = 1,		// main window
	HEADING_COLOR,
	DBORDER_COLOR,			// down interfaces
	DHEADING_COLOR,
	UBORDER_COLOR,			// up interfaces
	UHEADING_COLOR,
	PBORDER_COLOR,			// popups
	PHEADING_COLOR,
	BULKTEXT_COLOR,			// bulk text (help)
};

// FIXME granularize things, make packet handler iret-like
static pthread_mutex_t bfl = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

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

static inline int
start_screen_update(void){
	int ret = OK;

	update_panels();
	return ret;
}

static inline int
finish_screen_update(void){
	if(doupdate() == ERR){
		return ERR;
	}
	return OK;
}

// Pass current number of columns
static int
setup_statusbar(int cols){
	if(cols < 0){
		return -1;
	}else if(cols < ANSITERM_COLS){
		cols = ANSITERM_COLS;
	}
	if(statuschars <= cols){
		const size_t s = cols + 1;
		char *sm;

		if((sm = realloc(statusmsg,s)) == NULL){
			return -1;
		}
		statuschars = s;
		if(statusmsg == NULL){
			time_t t = time(NULL);
			struct tm tm;

			if(localtime_r(&t,&tm)){
				strftime(sm,s,"launched at %T",&tm);
			}else{
				sm[0] = '\0';
			}
		}
		statusmsg = sm;
	}
	return 0;
}

static inline int
interface_sniffing_p(const interface *i){
	return (i->rfd >= 0);
}

static inline int
interface_up_p(const interface *i){
	return (i->flags & IFF_UP);
}

static inline int
interface_carrier_p(const interface *i){
	return (i->flags & IFF_LOWER_UP);
}

static inline int
interface_promisc_p(const interface *i){
	return (i->flags & IFF_PROMISC);
}

static int
iface_optstr(WINDOW *w,const char *str,int hcolor,int bcolor){
	if(wcolor_set(w,bcolor,NULL) != OK){
		return -1;
	}
	if(waddch(w,'|') == ERR){
		return -1;
	}
	if(wcolor_set(w,hcolor,NULL) != OK){
		return -1;
	}
	if(waddstr(w,str) == ERR){
		return -1;
	}
	return 0;
}

static const char *
duplexstr(unsigned dplx){
	switch(dplx){
		case DUPLEX_FULL: return "full"; break;
		case DUPLEX_HALF: return "half"; break;
		default: break;
	}
	return "";
}

static const char *
modestr(unsigned dplx){
	switch(dplx){
		case NL80211_IFTYPE_UNSPECIFIED: return "auto"; break;
		case NL80211_IFTYPE_ADHOC: return "adhoc"; break;
		case NL80211_IFTYPE_STATION: return "managed"; break;
		case NL80211_IFTYPE_AP: return "ap"; break;
		case NL80211_IFTYPE_AP_VLAN: return "apvlan"; break;
		case NL80211_IFTYPE_WDS: return "wds"; break;
		case NL80211_IFTYPE_MONITOR: return "monitor"; break;
		case NL80211_IFTYPE_MESH_POINT: return "mesh"; break;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)
		case NL80211_IFTYPE_P2P_CLIENT: return "p2pclient"; break;
		case NL80211_IFTYPE_P2P_GO: return "p2pgo"; break;
#endif
		default: break;
	}
	return "";
}

#define ERREXIT endwin() ; fprintf(stderr,"ncurses failure|%s|%d\n",__func__,__LINE__); abort() ; goto err
// to be called only while ncurses lock is held
static int
iface_box(WINDOW *w,const interface *i,const iface_state *is){
	int bcolor,hcolor;
	size_t buslen;
	int attrs;

	// FIXME shouldn't have to know IFF_UP out here
	bcolor = interface_up_p(i) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = interface_up_p(i) ? UHEADING_COLOR : DHEADING_COLOR;
	attrs = ((is == current_iface) ? A_REVERSE : 0) | A_BOLD;
	assert(wattron(w,attrs | COLOR_PAIR(bcolor)) == OK);
	assert(box(w,0,0) == OK);
	assert(wattroff(w,A_REVERSE) == OK);
	assert(mvwprintw(w,0,START_COL,"[") != ERR);
	assert(wcolor_set(w,hcolor,NULL) == OK);
	assert(waddstr(w,i->name) != ERR);
	assert(wprintw(w," (%s",is->typestr) != ERR);
	if(strlen(i->drv.driver)){
		assert(waddch(w,' ') != ERR);
		assert(waddstr(w,i->drv.driver) != ERR);
		if(strlen(i->drv.version)){
			assert(wprintw(w," %s",i->drv.version) != ERR);
		}
		if(strlen(i->drv.fw_version)){
			assert(wprintw(w," fw %s",i->drv.fw_version) != ERR);
		}
	}
	assert(waddch(w,')') != ERR);
	assert(wcolor_set(w,bcolor,NULL) != ERR);
	assert(wprintw(w,"]") != ERR);
	assert(wattron(w,attrs) != ERR);
	assert(wattroff(w,A_REVERSE) != ERR);
	assert(mvwprintw(w,PAD_LINES - 1,START_COL * 2,"[") != ERR);
	assert(wcolor_set(w,hcolor,NULL) != ERR);
	assert(wprintw(w,"mtu %d",i->mtu) != ERR);
	if(interface_up_p(i)){
		if(iface_optstr(w,"up",hcolor,bcolor)){
			ERREXIT;
		}
		if(!interface_carrier_p(i)){
			if(iface_optstr(w,"no carrier",hcolor,bcolor)){
				ERREXIT;
			}
		}else if(i->settings_valid == SETTINGS_VALID_ETHTOOL){
			if(wprintw(w," (%uMb %s)",i->settings.speed,duplexstr(i->settings.duplex)) == ERR){
				ERREXIT;
			}
		}else if(i->settings_valid == SETTINGS_VALID_WEXT){
			if(wprintw(w," (%uMb %s)",i->wireless.bitrate / 1000000u,modestr(i->wireless.mode)) == ERR){
				ERREXIT;
			}
		}
	}else{
		if(iface_optstr(w,"down",hcolor,bcolor)){
			ERREXIT;
		}
		// FIXME find out whether carrier is meaningful for down
		// interfaces (i've not seen one)
	}
	if(interface_promisc_p(i)){
		if(iface_optstr(w,"promisc",hcolor,bcolor)){
			ERREXIT;
		}
	}
	if(wcolor_set(w,bcolor,NULL)){
		ERREXIT;
	}
	if(wprintw(w,"]") < 0){
		ERREXIT;
	}
	if(wattroff(w,A_BOLD) != OK){
		ERREXIT;
	}
	if( (buslen = strlen(i->drv.bus_info)) ){
		if(i->busname){
			buslen += strlen(i->busname) + 1;
			if(mvwprintw(w,PAD_LINES - 1,COLS - (buslen + 3 + START_COL),
					"%s:%s",i->busname,i->drv.bus_info) != OK){
				ERREXIT;
			}
		}else if(mvwprintw(w,PAD_LINES - 1,COLS - (buslen + 3 + START_COL),
					"%s",i->drv.bus_info) != OK){
			ERREXIT;
		}
	}
	if(wcolor_set(w,0,NULL) != OK){
		ERREXIT;
	}
	if(wattroff(w,attrs) != OK){
		ERREXIT;
	}
	return 0;

err:
	return -1;
}

// to be called only while ncurses lock is held
static int
draw_main_window(WINDOW *w,const char *name,const char *ver){
	int rows,cols;

	getmaxyx(w,rows,cols);
	if(setup_statusbar(cols)){
		ERREXIT;
	}
	if(wcolor_set(w,BORDER_COLOR,NULL) != OK){
		ERREXIT;
	}
	if(box(w,0,0) != OK){
		ERREXIT;
	}
	if(mvwprintw(w,0,2,"[") < 0){
		ERREXIT;
	}
	if(wattron(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
		ERREXIT;
	}
	if(wprintw(w,"%s %s on %s %s (libc %s-%s)",name,ver,sysuts.sysname,
				sysuts.release,glibc_version,glibc_release) < 0){
		ERREXIT;
	}
	if(wattroff(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
		ERREXIT;
	}
	if(wcolor_set(w,BORDER_COLOR,NULL) != OK){
		ERREXIT;
	}
	if(wprintw(w,"]") < 0){
		ERREXIT;
	}
	if(wattron(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != OK){
		ERREXIT;
	}
	// addstr() doesn't interpret format strings, so this is safe. It will
	// fail, however, if the string can't fit on the window, which will for
	// instance happen if there's an embedded newline.
	mvwaddstr(w,rows - 1,START_COL,statusmsg);
	if(wattroff(w,A_BOLD | COLOR_PAIR(BORDER_COLOR)) != OK){
		ERREXIT;
	}
	if(wcolor_set(w,0,NULL) != OK){
		ERREXIT;
	}
	if(start_screen_update() == ERR){
		ERREXIT;
	}
	if(finish_screen_update() == ERR){
		ERREXIT;
	}
	return 0;

err:
	return -1;
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
	return ret;
}

// NULL fmt clears the status bar
static int
wstatus_locked(WINDOW *w,const char *fmt,...){
	va_list va;
	int ret;

	va_start(va,fmt);
	ret = wvstatus_locked(w,fmt,va);
	va_end(va);
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

static const interface *
get_current_iface(void){
	if(current_iface){
		return iface_by_idx(current_iface->ifacenum);
	}
	return NULL;
}

static void
toggle_promisc_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(interface_promisc_p(i)){
			wstatus_locked(w,"Disabling promiscuity on %s",i->name);
			disable_promiscuity(octx,i);
		}else{
			wstatus_locked(w,"Enabling promiscuity on %s",i->name);
			enable_promiscuity(octx,i);
		}
	}
}

static void
sniff_interface_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(!interface_sniffing_p(i)){
			if(!interface_up_p(i)){
				wstatus_locked(w,"Bringing up %s...",i->name);
				up_interface(octx,i);
			}
		}else{
			// FIXME send request to stop sniffing
		}
	}
}

static void
down_interface_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(interface_up_p(i)){
			wstatus_locked(w,"Bringing down %s...",i->name);
			down_interface(octx,i);
		}
	}
}

static void
use_next_iface_locked(void){
	if(current_iface && current_iface->next != current_iface){
		const iface_state *is = current_iface;
		interface *i = iface_by_idx(is->ifacenum);

		current_iface = current_iface->next;
		iface_box(is->subwin,i,is);
		is = current_iface;
		i = iface_by_idx(is->ifacenum);
		iface_box(is->subwin,i,is);
		start_screen_update();
		finish_screen_update();
	}
}

static void
use_prev_iface_locked(void){
	if(current_iface && current_iface->prev != current_iface){
		const iface_state *is = current_iface;
		interface *i = iface_by_idx(is->ifacenum);

		current_iface = current_iface->prev;
		iface_box(is->subwin,i,is);
		is = current_iface;
		i = iface_by_idx(is->ifacenum);
		iface_box(is->subwin,i,is);
		start_screen_update();
		finish_screen_update();
	}
}

// FIXME we ought precreate the help screen, and show/hide it rather than
// creating and destroying it every time.
struct panel_state {
	PANEL *p;
	WINDOW *w;
};

static void
hide_panel_locked(WINDOW *w,struct panel_state *ps){
	if(ps){
		hide_panel(ps->p);
		del_panel(ps->p);
		ps->p = NULL;
		delwin(ps->w);
		ps->w = NULL;
		start_screen_update();
		draw_main_window(w,PROGNAME,VERSION);
		finish_screen_update();
	}
}

static const wchar_t *helps[] = {
	L"'k'/'↑' (up arrow): previous interface",
	L"'j'/'↓' (down arrow): next interface",
	L"'P': preferences",
	L"       configure persistent or temporary program settings",
	L"'n': network configuration",
	L"       configure addresses, routes, bridges, and wireless",
	L"'a': attack configuration",
	L"       configure source quenching, assassinations, and deauthentication",
	L"'j': hijack configuration",
	L"       configure fake APs, rogue DHCP/DNS, and ARP MitM",
	L"'d': defense configuration",
	L"       define authoritative configurations to enforce",
	L"'S': secrets database",
	L"       export pilfered passwords, cookies, and identifying data",
	L"'c': crypto configuration",
	L"       configure algorithm stepdown, WEP/WPA cracking, SSL MitM",
	L"'p': toggle promiscuity",
	L"'s': toggle sniffing, bringing up interface if down",
	L"'d': bring down device",
	L"'v': view detailed interface info/statistics",
	L"'m': change device MAC",
	L"'u': change device MTU",
	L"'h': toggle this help display",
	L"'q': quit",
	NULL
};

// FIXME need to support scrolling through the list
static int
helpstrs(WINDOW *hw,int row,int col,int rows){
	const wchar_t *hs;
	int z;

	for(z = 0 ; (hs = helps[z]) && z < rows ; ++z){
		if(mvwaddwstr(hw,row + z,col,hs) == ERR){
			return -1;
		}
	}
	return 0;
}

// Can leak resources on failure -- caller must free window/panel on error
static int
new_display_panel(struct panel_state *ps,int rows,int cols,int srow,int scol){
	assert((ps->w = newwin(rows,cols,srow,scol)) != NULL);
	assert((ps->p = new_panel(ps->w)) != NULL);
	assert(wattron(ps->w,A_BOLD) != ERR);
	assert(wcolor_set(ps->w,PBORDER_COLOR,NULL) == OK);
	assert(box(ps->w,0,0) == OK);
	assert(wattroff(ps->w,A_BOLD) != ERR);
	return OK;
}

#define DETAILS_ROWS 8 // FIXME
static int
display_details_locked(WINDOW *mainw,struct panel_state *ps){
	// The NULL doesn't count as a row
	int rows,cols,startrow;

	memset(ps,0,sizeof(*ps));
	getmaxyx(mainw,rows,cols);
	if(cols < START_COL * 2 + 1){
		ERREXIT;
	}
	// Space for the status bar + gap, bottom bar + gap,
	// and top bar + gap
	startrow = rows - (START_LINE * 3 + DETAILS_ROWS);
	if(rows <= startrow){
		ERREXIT;
	}
	rows -= startrow + START_LINE;
	cols -= START_COL * 2;
	if(new_display_panel(ps,rows,cols,startrow,START_COL)){
		ERREXIT;
	}
	assert(wcolor_set(ps->w,PHEADING_COLOR,NULL) == OK);
	assert(mvwprintw(ps->w,0,START_COL * 2,"press 'v' to dismiss details") != ERR);
	assert(start_screen_update() != ERR);
	assert(finish_screen_update() != ERR);
	return 0;

err:
	if(ps->p){
		hide_panel(ps->p);
		del_panel(ps->p);
	}
	if(ps->w){
		delwin(ps->w);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}

static int
display_help_locked(WINDOW *mainw,struct panel_state *ps){
	const wchar_t crightstr[] = L"copyright © 2011 nick black";
	const int crightlen = wcslen(crightstr);
	// The NULL doesn't count as a row
	const int helprows = sizeof(helps) / sizeof(*helps) - 1;
	int rows,cols,startrow;

	memset(ps,0,sizeof(*ps));
	getmaxyx(mainw,rows,cols);
	if(cols < crightlen + START_COL * 2){
		ERREXIT;
	}
	// Optimally, we get space for the status bar + gap, bottom bar + gap,
	// and top bar + gap. We might get less.
	startrow = rows - (START_LINE * 3 + helprows);
	// Need to support scrolling for this to work! FIXME
	if(startrow <= START_LINE + PAD_LINES + 1){
		startrow = START_LINE + PAD_LINES + 1;
	}
	// We get all the rows from startrow to the last two.
	assert(startrow + START_LINE < rows);
	rows -= startrow + START_LINE;
	cols -= START_COL * 2;
	if(new_display_panel(ps,rows,cols,startrow,START_COL)){
		ERREXIT;
	}
	assert(wcolor_set(ps->w,PHEADING_COLOR,NULL) == OK);
	assert(mvwprintw(ps->w,0,START_COL * 2,"press 'h' to dismiss help") != ERR);
	assert(mvwaddwstr(ps->w,rows - 1,cols - (crightlen + START_COL * 2),crightstr) != ERR);
	assert(wcolor_set(ps->w,BULKTEXT_COLOR,NULL) == OK);
	if(helpstrs(ps->w,START_LINE,START_COL,rows - START_LINE * 2)){
		// FIXME need to support scrolling!
		ERREXIT;
	}
	if(start_screen_update() == ERR){
		ERREXIT;
	}
	if(finish_screen_update() == ERR){
		ERREXIT;
	}
	return 0;

err:
	if(ps->p){
		hide_panel(ps->p);
		del_panel(ps->p);
	}
	if(ps->w){
		delwin(ps->w);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}

struct ncurses_input_marshal {
	WINDOW *w;
	PANEL *p;
	const omphalos_iface *octx;
};

static void *
ncurses_input_thread(void *unsafe_marsh){
	struct ncurses_input_marshal *nim = unsafe_marsh;
	struct panel_state help,details,*active;
	const omphalos_iface *octx = nim->octx;
	WINDOW *w = nim->w;
	int ch;

	active = NULL; // No subpanels initially
	memset(&help,0,sizeof(help));
	memset(&details,0,sizeof(details));
	while((ch = getch()) != 'q' && ch != 'Q'){
	switch(ch){
		case KEY_UP: case 'k':
			pthread_mutex_lock(&bfl);
				use_prev_iface_locked();
			pthread_mutex_unlock(&bfl);
			break;
		case KEY_DOWN: case 'j':
			pthread_mutex_lock(&bfl);
				use_next_iface_locked();
			pthread_mutex_unlock(&bfl);
			break;
		case 'p':
			pthread_mutex_lock(&bfl);
				toggle_promisc_locked(octx,w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'd':
			pthread_mutex_lock(&bfl);
				down_interface_locked(octx,w);
			pthread_mutex_unlock(&bfl);
			break;
		case 's':
			pthread_mutex_lock(&bfl);
				sniff_interface_locked(octx,w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'v':{
			pthread_mutex_lock(&bfl);
			if(details.w){
				hide_panel_locked(w,&details);
				active = NULL;
			}else{
				hide_panel_locked(w,active);
				active = (display_details_locked(w,&details) == OK)
					? &details : NULL;
			}
			pthread_mutex_unlock(&bfl);
			break;
		}case 'h':{
			pthread_mutex_lock(&bfl);
			if(help.w){
				hide_panel_locked(w,&help);
				active = NULL;
			}else{
				hide_panel_locked(w,active);
				active = (display_help_locked(w,&help) == OK)
					? &help : NULL;
			}
			pthread_mutex_unlock(&bfl);
			break;
		}default:{
			const char *hstr = !help.w ? " ('h' for help)" : "";
			if(isprint(ch)){
				wstatus(w,"unknown command '%c'%s",ch,hstr);
			}else{
				wstatus(w,"unknown scancode %d%s",ch,hstr);
			}
			break;
		}
	}
	}
	wstatus(w,"%s","shutting down");
	// we can't use raise() here, as that sends the signal only
	// to ourselves, and we have it masked.
	kill(getpid(),SIGINT);
	pthread_exit(NULL);
}

// Cleanup which ought be performed even if we had a failure elsewhere, or
// indeed never started.
static int
mandatory_cleanup(WINDOW **w,PANEL **p){
	int ret = 0;

	pthread_mutex_lock(&bfl);
	if(*p){
		if(del_panel(*p) == ERR){
			ret = -4;
		}
		*p = NULL;
	}
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
	case -4: fprintf(stderr,"Couldn't destroy main panel\n"); break;
	case -3: fprintf(stderr,"Couldn't end main window\n"); break;
	case -2: fprintf(stderr,"Couldn't delete main window\n"); break;
	case -1: fprintf(stderr,"Couldn't delete main pad\n"); break;
	case 0: break;
	default: fprintf(stderr,"Couldn't cleanup ncurses\n"); break;
	}
	return ret;
}

static WINDOW *
ncurses_setup(const omphalos_iface *octx,PANEL **panel){
	struct ncurses_input_marshal *nim;
	const char *errstr = NULL;
	WINDOW *w = NULL;
	PANEL *p = NULL;

	if(initscr() == NULL){
		fprintf(stderr,"Couldn't initialize ncurses\n");
		return NULL;
	}
	if(cbreak() != OK){
		errstr = "Couldn't disable input buffering\n";
		goto err;
	}
	if(noecho() != OK){
		errstr = "Couldn't disable input echoing\n";
		goto err;
	}
	if(intrflush(stdscr,TRUE) != OK){
		errstr = "Couldn't set flush-on-interrupt\n";
		goto err;
	}
	if(nonl() != OK){
		errstr = "Couldn't disable nl translation\n";
		goto err;
	}
	if(start_color() != OK){
		errstr = "Couldn't initialize ncurses color\n";
		goto err;
	}
	if(use_default_colors()){
		errstr = "Couldn't initialize ncurses colordefs\n";
		goto err;
	}
	w = stdscr;
	/*if((w = newpad(LINES,COLS)) == NULL){
		errstr = "Couldn't initialize main pad\n";
		goto err;
	}
	*/
	if((p = new_panel(stdscr)) == NULL){
		errstr = "Couldn't initialize main panel\n";
		goto err;
	}
	keypad(stdscr,TRUE);
	if(init_pair(BORDER_COLOR,COLOR_GREEN,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(HEADING_COLOR,COLOR_YELLOW,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(DBORDER_COLOR,COLOR_WHITE,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(DHEADING_COLOR,COLOR_WHITE,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(UBORDER_COLOR,COLOR_YELLOW,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(UHEADING_COLOR,COLOR_GREEN,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(PBORDER_COLOR,COLOR_CYAN,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(PHEADING_COLOR,COLOR_RED,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(BULKTEXT_COLOR,COLOR_WHITE,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(curs_set(0) == ERR){
		errstr = "Couldn't disable cursor\n";
		goto err;
	}
	if(setup_statusbar(COLS)){
		errstr = "Couldn't setup status bar\n";
		goto err;
	}
	if(draw_main_window(w,PROGNAME,VERSION)){
		errstr = "Couldn't use ncurses\n";
		goto err;
	}
	if((nim = malloc(sizeof(*nim))) == NULL){
		goto err;
	}
	nim->octx = octx;
	nim->w = w;
	nim->p = p;
	if(pthread_create(&inputtid,NULL,ncurses_input_thread,nim)){
		errstr = "Couldn't create UI thread\n";
		free(nim);
		goto err;
	}
	// FIXME install SIGWINCH() handler...
	*panel = p;
	return w;

err:
	mandatory_cleanup(&w,&p);
	fprintf(stderr,"%s",errstr);
	return NULL;
}

static int
print_iface_state(const interface *i __attribute__ ((unused)),const iface_state *is){
	if(mvwprintw(is->subwin,1,1,"pkts: %ju",i->frames) != OK){
		return -1;
	}
	if(start_screen_update() == ERR){
		return -1;
	}
	if(finish_screen_update() == ERR){
		return -1;
	}
	return 0;
}

static inline void
packet_cb_locked(const interface *i,iface_state *is){
	if(is){
		if(print_iface_state(i,is)){
			abort();
		}
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
		const char *tstr;

		if( (tstr = lookup_arptype(i->arptype)) ){
			if( (ret = malloc(sizeof(iface_state))) ){
				ret->typestr = tstr;
				ret->scrline = START_LINE + count_interface * (PAD_LINES + 1);
				ret->ifacenum = inum;
				if((ret->prev = current_iface) == NULL){
					current_iface = ret->prev = ret->next = ret;
				}else{
					// The order on screen must match the list order, so splice it onto
					// the end. We might be anywhere, so use absolute coords (scrline).
					while(ret->prev->next->scrline > ret->prev->scrline){
						ret->prev = ret->prev->next;
					}
					ret->next = ret->prev->next;
					ret->next->prev = ret;
					ret->prev->next = ret;
				}
				if( (ret->subwin = subwin(pad,PAD_LINES,PAD_COLS,ret->scrline,START_COL)) &&
						(ret->panel = new_panel(ret->subwin)) ){
					++count_interface;
				}else{
					delwin(ret->subwin);
					if(current_iface == ret){
						current_iface = NULL;
					}else{
						ret->next->prev = ret->prev;
						ret->prev->next = ret->next;
					}
					free(ret);
					ret = NULL;
				}
			}
		}
	}
	if(ret){
		iface_box(ret->subwin,i,ret);
		if(i->flags & IFF_UP){
			packet_cb_locked(i,ret);
		}
		start_screen_update();
		finish_screen_update();
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
		werase(is->subwin);
		del_panel(is->panel);
		delwin(is->subwin);
		if(is->next != is){
			is->next->prev = is->prev;
			is->prev->next = is->next;
			if(is == current_iface){
				current_iface = is->prev;
			}
		}else{
			current_iface = NULL;
		}
		free(is);
		start_screen_update();
		finish_screen_update();
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
	PANEL *panel;

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
	pctx.iface.packet_read = packet_callback;
	pctx.iface.iface_event = interface_callback;
	pctx.iface.iface_removed = interface_removed_callback;
	pctx.iface.diagnostic = diag_callback;
	if((pad = ncurses_setup(&pctx.iface,&panel)) == NULL){
		return EXIT_FAILURE;
	}
	if(omphalos_init(&pctx)){
		int err = errno;

		mandatory_cleanup(&pad,&panel);
		fprintf(stderr,"Error in omphalos_init() (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	omphalos_cleanup(&pctx);
	if(mandatory_cleanup(&pad,&panel)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
