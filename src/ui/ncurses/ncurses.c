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
#include <linux/nl80211.h>
#include <ncursesw/panel.h>
#include <linux/rtnetlink.h>
#include <omphalos/timing.h>
#include <ncursesw/ncurses.h>
#include <omphalos/hwaddrs.h>
#include <omphalos/ethtool.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>
#include <gnu/libc-version.h>

// Add ((format (printf))) attributes to ncurses functions, which sadly
// lack them (at least as of Debian's 5.9-1).
extern int wprintw(WINDOW *,const char *,...) __attribute__ ((format (printf,2,3)));
extern int mvwprintw(WINDOW *,int,int,const char *,...) __attribute__ ((format (printf,4,5)));

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98-pre"	// FIXME

#define PAD_LINES 3
#define PAD_COLS(cols) ((cols) - START_COL * 2)
#define START_LINE 2
#define START_COL 1
#define U64STRLEN 20	// Does not include a '\0' (18,446,744,073,709,551,616)
#define U64FMT "%-20ju"
#define PREFIXSTRLEN U64STRLEN

// FIXME we ought precreate the subwindows, and show/hide them rather than
// creating and destroying them every time.
struct panel_state {
	PANEL *p;
	int ysize;			// number of lines of *text* (not win)
};

typedef struct l2obj {
	struct l2obj *next;
	struct l2host *l2;
} l2obj;

#define PANEL_STATE_INITIALIZER { .p = NULL, .ysize = -1, }

static struct panel_state *active;
static struct panel_state help = PANEL_STATE_INITIALIZER;
static struct panel_state details = PANEL_STATE_INITIALIZER;

// Bind one of these state structures to each interface in the callback,
// and also associate an iface with them via *iface (for UI actions).
typedef struct iface_state {
	interface *iface;		// corresponding omphalos iface struct
	int scrline;			// line within the containing pad
	int ysize;			// number of lines
	int l2ents;			// number of l2 entities
	int first_visible;		// index of first visible l2 entity
	WINDOW *subwin;			// subwin
	PANEL *panel;			// panel
	const char *typestr;		// looked up using iface->arptype
	struct timeval lastprinted;	// last time we printed the iface
	int alarmset;			// alarm set for UI update?
	int devaction;			// 1 == down, -1 == up, 0 == nothing
	l2obj *l2objs;			// l2 entity list
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
	BULKTEXT_COLOR,			// bulk text (help, details)
	IFACE_COLOR,			// interface summary text
	MCAST_COLOR,			// multicast addresses
	BCAST_COLOR,			// broadcast addresses
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

static inline int
full_screen_update(void){
	int ret;

	assert((ret = start_screen_update()) == 0);
	assert((ret |= finish_screen_update()) == 0);
	return ret;
}

// Pass current number of columns
static int
setup_statusbar(int cols){
	if(cols < 0){
		return -1;
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
		return ERR;
	}
	if(waddch(w,'|') == ERR){
		return ERR;
	}
	if(wcolor_set(w,hcolor,NULL) != OK){
		return ERR;
	}
	if(waddstr(w,str) == ERR){
		return ERR;
	}
	return OK;
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

// For full safety, pass in a buffer that can hold the decimal representation
// of the largest uintmax_t plus three (one for the unit, one for the decimal
// separator, and one for the NUL byte). If omitdec is non-zero, and the
// decimal portion is all 0's, the decimal portion will not be printed. decimal
// indicates scaling, and should be '1' if no scaling has taken place.
static char *
genprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec,
			unsigned mult,int uprefix){
	const char prefixes[] = "KMGTPEY";
	unsigned consumed = 0;
	uintmax_t div;

	div = mult;
	while((val / decimal) >= div && consumed < strlen(prefixes)){
		div *= mult;
		if(UINTMAX_MAX / div < mult){ // watch for overflow
			break;
		}
		++consumed;
	}
	if(div != mult){
		div /= mult;
		val /= decimal;
		if(val % div || omitdec == 0){
			snprintf(buf,bsize,"%ju.%02ju%c%c",val / div,(val % div) / ((div + 99) / 100),
					prefixes[consumed - 1],uprefix);
		}else{
			snprintf(buf,bsize,"%ju%c%c",val / div,prefixes[consumed - 1],uprefix);
		}
	}else{
		if(val % decimal || omitdec == 0){
			snprintf(buf,bsize,"%ju.%02ju",val / decimal,val % decimal);
		}else{
			snprintf(buf,bsize,"%ju",val / decimal);
		}
	}
	return buf;
}

static inline char *
prefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1000,'\0');
}

static inline char *
bprefix(uintmax_t val,unsigned decimal,char *buf,size_t bsize,int omitdec){
	return genprefix(val,decimal,buf,bsize,omitdec,1024,'i');
}

#define ERREXIT endwin() ; fprintf(stderr,"ncurses failure|%s|%d\n",__func__,__LINE__); abort() ; goto err
// to be called only while ncurses lock is held
static int
iface_box(WINDOW *w,const interface *i,const iface_state *is){
	int bcolor,hcolor,scrrows,scrcols;
	size_t buslen;
	int attrs;

	getmaxyx(w,scrrows,scrcols);
	assert(scrrows); // FIXME
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
	assert(mvwprintw(w,is->ysize - 1,START_COL * 2,"[") != ERR);
	assert(wcolor_set(w,hcolor,NULL) != ERR);
	assert(wprintw(w,"mtu %d",i->mtu) != ERR);
	if(interface_up_p(i)){
		char buf[U64STRLEN + 1];

		assert(iface_optstr(w,"up",hcolor,bcolor) != ERR);
		if(!interface_carrier_p(i)){
			assert(waddstr(w," (no carrier)") != ERR);
		}else if(i->settings_valid == SETTINGS_VALID_ETHTOOL){
			assert(wprintw(w," (%sb %s)",prefix(i->settings.ethtool.speed * 1000000u,1,buf,sizeof(buf),1),
						duplexstr(i->settings.ethtool.duplex)) != ERR);
		}else if(i->settings_valid == SETTINGS_VALID_WEXT){
			assert(wprintw(w," (%sb %s)",prefix(i->settings.wext.bitrate,1,buf,sizeof(buf),1),modestr(i->settings.wext.mode)) != ERR);
		}
	}else{
		assert(iface_optstr(w,"down",hcolor,bcolor) != ERR);
	}
	if(interface_promisc_p(i)){
		assert(iface_optstr(w,"promisc",hcolor,bcolor) != ERR);
	}
	assert(wcolor_set(w,bcolor,NULL) != ERR);
	assert(wprintw(w,"]") != ERR);
	assert(wattroff(w,A_BOLD) != ERR);
	if( (buslen = strlen(i->drv.bus_info)) ){
		if(i->busname){
			buslen += strlen(i->busname) + 1;
			assert(mvwprintw(w,is->ysize - 1,scrcols - (buslen + 3 + START_COL),
					"%s:%s",i->busname,i->drv.bus_info) != ERR);
		}else{
			assert(mvwprintw(w,is->ysize - 1,scrcols - (buslen + 3 + START_COL),
					"%s",i->drv.bus_info) != ERR);
		}
	}
	assert(wcolor_set(w,0,NULL) != ERR);
	assert(wattroff(w,attrs) != ERR);
	return 0;
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
	assert(mvwaddstr(w,rows - 1,START_COL * 2,statusmsg) != ERR);
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
		return current_iface->iface;
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
				current_iface->devaction = -1;
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
			current_iface->devaction = 1;
			down_interface(octx,i);
		}
	}
}

static void
hide_panel_locked(WINDOW *w,struct panel_state *ps){
	if(ps){
		WINDOW *psw;

		psw = panel_window(ps->p);
		hide_panel(ps->p);
		del_panel(ps->p);
		ps->p = NULL;
		delwin(psw);
		ps->ysize = -1;
		start_screen_update();
		draw_main_window(w,PROGNAME,VERSION);
		finish_screen_update();
	}
}

// Create a panel at the bottom of the window, referred to as the "subdisplay".
// Only one can currently be active at a time. Window decoration and placement
// is managed here; only the rows needed for display ought be provided.
static int
new_display_panel(WINDOW *w,struct panel_state *ps,int rows,const wchar_t *hstr){
	const wchar_t crightstr[] = L"copyright © 2011 nick black";
	const int crightlen = wcslen(crightstr);
	WINDOW *psw;
	int x,y;

	getmaxyx(w,y,x);
	assert(y >= rows + 3);
	assert(x >= crightlen + START_COL * 3);
	assert( (psw = newwin(rows + 2,x - START_COL * 2,y - (rows + 3),1)) );
	if(psw == NULL){
		return ERR;
	}
	assert((ps->p = new_panel(psw)));
	if(ps->p == NULL){
		delwin(psw);
		return ERR;
	}
	ps->ysize = rows;
	// memory leaks follow if we're compiled with NDEBUG! FIXME
	assert(wattron(psw,A_BOLD) != ERR);
	assert(wcolor_set(psw,PBORDER_COLOR,NULL) == OK);
	assert(box(psw,0,0) == OK);
	assert(wattroff(psw,A_BOLD) != ERR);
	assert(wcolor_set(psw,PHEADING_COLOR,NULL) == OK);
	assert(mvwaddwstr(psw,0,START_COL * 2,hstr) != ERR);
	assert(mvwaddwstr(psw,rows + 1,x - (crightlen + START_COL * 4),crightstr) != ERR);
	assert(wcolor_set(psw,BULKTEXT_COLOR,NULL) == OK);
	return OK;
}

static int
offload_details(WINDOW *w,const interface *i,int row,int col,const char *name,
						unsigned val){
	int r;

	r = iface_offloaded_p(i,val);
	return mvwprintw(w,row,col,"%s%c",name,r > 0 ? '+' : r < 0 ? '?' : '-');
}

#define DETAILROWS 8

// FIXME need to support scrolling through the output
static int
iface_details(WINDOW *hw,const interface *i,int rows){
	const int col = START_COL;
	int scrcols,scrrows;
	const int row = 1;
	int z;

	getmaxyx(hw,scrrows,scrcols);
	assert(scrrows); // FIXME
	if((z = rows) >= DETAILROWS){
		z = DETAILROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (DETAILROWS - 1):{
		assert(mvwprintw(hw,row + z,col,"drops: "U64FMT" truncs: "U64FMT" (%ju recov)",
					i->drops,i->truncated,i->truncated_recovered) != ERR);
		--z;
	}case 6:{
		assert(mvwprintw(hw,row + z,col,"mform: "U64FMT" noprot: "U64FMT,
					i->malformed,i->noprotocol) != ERR);
		--z;
	}case 5:{
		assert(mvwprintw(hw,row + z,col,"bytes: "U64FMT" frames: "U64FMT,
					i->bytes,i->frames) != ERR);
		--z;
	}case 4:{
		char buf[U64STRLEN];
		assert(mvwprintw(hw,row + z,col,"RXfd: %-4d fbytes: %-6u fnum: %-8u bsize: %sB bnum: %-8u",
					i->rfd,i->rtpr.tp_frame_size,i->rtpr.tp_frame_nr,
					bprefix(i->rtpr.tp_block_size,1,buf,sizeof(buf),1),i->rtpr.tp_block_nr) != ERR);
		--z;
	}case 3:{
		char buf[U64STRLEN];
		assert(mvwprintw(hw,row + z,col,"TXfd: %-4d fbytes: %-6u fnum: %-8u bsize: %sB bnum: %-8u",
					i->fd,i->ttpr.tp_frame_size,i->ttpr.tp_frame_nr,
					bprefix(i->ttpr.tp_block_size,1,buf,sizeof(buf),1),i->ttpr.tp_block_nr) != ERR);
		--z;
	}case 2:{
		assert(offload_details(hw,i,row + z,col,"TSO",TCP_SEG_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 5,"S/G",ETH_SCATTER_GATHER) != ERR);
		assert(offload_details(hw,i,row + z,col + 10,"UFO",UDP_FRAG_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 15,"GSO",GEN_SEG_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 20,"GRO",GENRX_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 25,"LRO",LARGERX_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 30,"TCsm",TX_CSUM_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 36,"RCsm",RX_CSUM_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 42,"TVln",TXVLAN_OFFLOAD) != ERR);
		assert(offload_details(hw,i,row + z,col + 48,"RVln",RXVLAN_OFFLOAD) != ERR);
		--z;
	}case 1:{
		assert(mvwprintw(hw,row + z,col,"%-*s",scrcols - 2,i->topinfo.devname ?
					i->topinfo.devname : "Unknown device") != ERR);
		--z;
	}case 0:{
		char buf[PREFIXSTRLEN],buf2[PREFIXSTRLEN];
		if(i->flags & IFF_NOARP){
			assert(mvwprintw(hw,row + z,col,"%-16s %*s txr: %sB\t rxr: %sB\t mtu: %-6d",
						i->name,IFHWADDRLEN * 3 - 1,"",
						bprefix(i->ts,1,buf,sizeof(buf),1),
						bprefix(i->rs,1,buf2,sizeof(buf2),1),i->mtu) != ERR);
		}else{
			char *mac;

			if((mac = hwaddrstr(i)) == NULL){
				return ERR;
			}
			assert(mvwprintw(hw,row + z,col,"%-16s %*s txr: %sB\t rxr: %sB\t mtu: %-6d",
						i->name,IFHWADDRLEN * 3 - 1,mac,
						bprefix(i->ts,1,buf,sizeof(buf),1),
						bprefix(i->rs,1,buf2,sizeof(buf2),1),i->mtu) != ERR);
			free(mac);
		}
		--z;
		break;
	}default:{
		return ERR;
	} }
	return OK;
}

static int
display_details_locked(WINDOW *mainw,struct panel_state *ps,iface_state *is){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,DETAILROWS,L"press 'v' to dismiss details")){
		ERREXIT;
	}
	if(is){
		if(iface_details(panel_window(ps->p),is->iface,ps->ysize)){
			ERREXIT;
		}
	}
	assert(start_screen_update() != ERR);
	assert(finish_screen_update() != ERR);
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

static const wchar_t *helps[] = {
	L"'k'/'↑' (up arrow): previous interface",
	L"'j'/'↓' (down arrow): next interface",
	L"'^L' (ctrl + 'L'): redraw the screen",
	L"'P': preferences",
	L"       configure persistent or temporary program settings",
	L"'n': network configuration",
	L"       configure addresses, routes, bridges, and wireless",
	L"'a': attack configuration",
	L"       configure source quenching, assassinations, and deauthentication",
	L"'J': hijack configuration",
	L"       configure fake APs, rogue DHCP/DNS, and ARP MitM",
	L"'D': defense configuration",
	L"       define authoritative configurations to enforce",
	L"'S': secrets database",
	L"       export pilfered passwords, cookies, and identifying data",
	L"'c': crypto configuration",
	L"       configure algorithm stepdown, WEP/WPA cracking, SSL MitM",
	L"'r': reset interface's stats",
	L"'R': reset all interfaces' stats",
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

static int
display_help_locked(WINDOW *mainw,struct panel_state *ps){
	const int helprows = sizeof(helps) / sizeof(*helps) - 1; // NULL != row

	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,helprows,L"press 'h' to dismiss help")){
		ERREXIT;
	}
	if(helpstrs(panel_window(ps->p),1,ps->ysize)){
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
		WINDOW *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}

// FIXME eliminate all callers!
static void
unimplemented(WINDOW *w,const void *v){
	wstatus_locked(w,"Sorry bro; that ain't implemented yet (%p)!",v);
}

static void
configure_prefs(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_network(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_attacks(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_hijacks(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_defence(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_secrets(WINDOW *w){
	unimplemented(w,NULL);
}

static void
configure_crypto(WINDOW *w){
	unimplemented(w,NULL);
}

static void
change_mac(WINDOW *w){
	unimplemented(w,NULL);
}

static void
change_mtu(WINDOW *w){
	unimplemented(w,NULL);
}

static void
reset_interface_stats(WINDOW *w,const interface *i){
	unimplemented(w,i);
}

static void
reset_all_interface_stats(WINDOW *w){
	iface_state *is;

	if( (is = current_iface) ){
		do{
			const interface *i = get_current_iface(); // FIXME get_iface(is);

			reset_interface_stats(w,i);
		}while((is = is->next) != current_iface);
	}
}

static void
reset_current_interface_stats(WINDOW *w){
	const interface *i;

	if( (i = get_current_iface()) ){
		reset_interface_stats(w,i);
	}
}

static void
use_next_iface_locked(void){
	if(current_iface && current_iface->next != current_iface){
		const iface_state *is = current_iface;
		interface *i = is->iface;

		current_iface = current_iface->next;
		iface_box(is->subwin,i,is);
		is = current_iface;
		i = is->iface;
		iface_box(is->subwin,i,is);
		if(details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		start_screen_update();
		finish_screen_update();
	}
}

static void
use_prev_iface_locked(void){
	if(current_iface && current_iface->prev != current_iface){
		const iface_state *is = current_iface;
		interface *i = is->iface;

		current_iface = current_iface->prev;
		iface_box(is->subwin,i,is);
		is = current_iface;
		i = is->iface;
		iface_box(is->subwin,i,is);
		if(details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		start_screen_update();
		finish_screen_update();
	}
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
			pthread_mutex_lock(&bfl);
				use_prev_iface_locked();
			pthread_mutex_unlock(&bfl);
			break;
		case KEY_DOWN: case 'j':
			pthread_mutex_lock(&bfl);
				use_next_iface_locked();
			pthread_mutex_unlock(&bfl);
			break;
		case 12: // Ctrl-L FIXME
			pthread_mutex_lock(&bfl);
				redrawwin(w);
				if(active){
					redrawwin(panel_window(active->p));
				}
				start_screen_update();
				finish_screen_update();
			pthread_mutex_unlock(&bfl);
			break;
		case 'P':
			pthread_mutex_lock(&bfl);
				configure_prefs(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'n':
			pthread_mutex_lock(&bfl);
				configure_network(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'a':
			pthread_mutex_lock(&bfl);
				configure_attacks(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'J':
			pthread_mutex_lock(&bfl);
				configure_hijacks(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'D':
			pthread_mutex_lock(&bfl);
				configure_defence(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'S':
			pthread_mutex_lock(&bfl);
				configure_secrets(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'c':
			pthread_mutex_lock(&bfl);
				configure_crypto(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'R':
			pthread_mutex_lock(&bfl);
				reset_all_interface_stats(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'r':
			pthread_mutex_lock(&bfl);
				reset_current_interface_stats(w);
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
		case 'u':
			pthread_mutex_lock(&bfl);
				change_mtu(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'm':
			pthread_mutex_lock(&bfl);
				change_mac(w);
			pthread_mutex_unlock(&bfl);
			break;
		case 'v':{
			pthread_mutex_lock(&bfl);
			if(details.p){
				hide_panel_locked(w,&details);
				active = NULL;
			}else{
				hide_panel_locked(w,active);
				active = (display_details_locked(w,&details,current_iface) == OK)
					? &details : NULL;
			}
			pthread_mutex_unlock(&bfl);
			break;
		}case 'h':{
			pthread_mutex_lock(&bfl);
			if(help.p){
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
			const char *hstr = !help.p ? " ('h' for help)" : "";
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
	const char *errstr = NULL;
	WINDOW *w = NULL;

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
	if(init_pair(IFACE_COLOR,COLOR_WHITE,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(MCAST_COLOR,COLOR_CYAN,-1) != OK){
		errstr = "Couldn't initialize ncurses colorpair\n";
		goto err;
	}
	if(init_pair(BCAST_COLOR,COLOR_BLUE,-1) != OK){
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
	if(pthread_create(&inputtid,NULL,ncurses_input_thread,nim)){
		errstr = "Couldn't create UI thread\n";
		free(nim);
		goto err;
	}
	// FIXME install SIGWINCH() handler...?
	return w;

err:
	mandatory_cleanup(&w);
	fprintf(stderr,"%s",errstr);
	return NULL;
}

static int
print_iface_state(const interface *i,const iface_state *is){
	char buf[U64STRLEN + 1],buf2[U64STRLEN + 1];
	unsigned long usecdomain;

	assert(wattron(is->subwin,A_BOLD | COLOR_PAIR(IFACE_COLOR)) == OK);
	// FIXME broken if bps domain ever != fps domain. need unite those
	// into one FTD stat by letting it take an object...
	usecdomain = i->bps.usec * i->bps.total;
	assert(mvwprintw(is->subwin,1,START_COL,"Last %lus: %7sb/s %7sp/s",
				usecdomain / 1000000,
				prefix(timestat_val(&i->bps) * CHAR_BIT * 1000000 * 100 / usecdomain,100,buf,sizeof(buf),0),
				prefix(timestat_val(&i->fps) * 1000000 * 100 / usecdomain,100,buf2,sizeof(buf2),0)) != ERR);
	return 0;
}

static int
print_iface_hosts(const interface *i,const iface_state *is){
	int line = 1,idx = 0;
	const l2obj *l;

	for(l = is->l2objs ; l ; ++idx, l = l->next){
		int cat = l2categorize(i,l->l2);
		char legend;
		char *hw;
		
		if(++idx < is->first_visible){
			continue;
		/*}else if(idx - is->first_visible > is->ysize){
			break;*/
		}
		switch(cat){
			case RTN_UNICAST:
				assert(wattrset(is->subwin,COLOR_PAIR(MCAST_COLOR)) != ERR);
				legend = 'U';
				break;
			case RTN_LOCAL:
				assert(wattrset(is->subwin,A_BOLD | COLOR_PAIR(MCAST_COLOR)) != ERR);
				legend = 'L';
				break;
			case RTN_MULTICAST:
				assert(wattrset(is->subwin,A_BOLD | COLOR_PAIR(BCAST_COLOR)) != ERR);
				legend = 'M';
				break;
			case RTN_BROADCAST:
				assert(wattrset(is->subwin,COLOR_PAIR(BCAST_COLOR)) != ERR);
				legend = 'B';
				break;
		}
		if((hw = l2addrstr(l->l2,i->addrlen)) == NULL){
			return ERR;
		}
		assert(mvwprintw(is->subwin,++line,START_COL," %c %s",legend,hw) != ERR);
		free(hw);
	}
	return OK;
}

static inline int
lines_for_interface(const interface *i,const iface_state *is){
	if(i->flags & IFF_UP){
		return PAD_LINES + is->l2ents;
	}else{
		return PAD_LINES - 1;
	}
}

static int
redraw_iface(const interface *i,iface_state *is){
	assert(iface_box(is->subwin,i,is) != ERR);
	if(i->flags & IFF_UP){
		if(print_iface_state(i,is)){
			return ERR;
		}
		if(print_iface_hosts(i,is)){
			return ERR;
		}
	}
	return OK;
}

// Is the interface window entirely visible?
static int
iface_visible_p(int rows,const iface_state *ret){
	if(ret->scrline + ret->ysize >= rows){
		return 0;
	}
	return 1;
}

static int
resize_iface(const interface *i,iface_state *ret){
	int rows,cols,nlines;

	getmaxyx(stdscr,rows,cols);
	// FIXME this only addresses expansion. need to handle shrinking, also.
	// (which can make a panel visible, just as expansion can hide it)
	if(!iface_visible_p(rows,ret)){
		return 0;
	}
	nlines = lines_for_interface(i,ret);
	if(nlines + ret->scrline >= rows){
		// FIXME we can expand up in this case
		return 0;
	}
	if(nlines != ret->ysize){ // aye, need resize
		int delta = nlines - ret->ysize;
		iface_state *is;

		for(is = ret->next ; is->scrline > ret->scrline ; is = is->next){
			interface *ii = is->iface;

			is->scrline += delta;
			assert(werase(is->subwin) == OK);
			if(iface_visible_p(rows,is)){
				assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
				if(redraw_iface(ii,is)){
					return ERR;
				}
			}else{
				// FIXME see if we can shrink it first!
				hide_panel(is->panel);
			}
		}
		ret->ysize = nlines;
		assert(werase(ret->subwin) != ERR);
		assert(wresize(ret->subwin,ret->ysize,PAD_COLS(cols)) != ERR);
		assert(replace_panel(ret->panel,ret->subwin) != ERR);
		if(redraw_iface(i,ret)){
			return ERR;
		}
		full_screen_update();
	}
	return OK;
}

static l2obj *
get_l2obj(struct l2host *l2){
	l2obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->l2 = l2;
	}
	return l;
}

static void
add_l2_to_iface(iface_state *is,l2obj *l2){
	l2obj **prev;

	++is->l2ents;
	for(prev = &is->l2objs ; *prev ; prev = &(*prev)->next){
		if(l2hostcmp(l2->l2,(*prev)->l2) < 0){
			break;
		}
	}
	l2->next = *prev;
	*prev = l2;
}

static void
handle_l2(const interface *i,iface_state *is,omphalos_packet *op){
	l2obj *news = NULL,*newd = NULL;

	if(l2host_get_opaque(op->l2s) == NULL){
		if((news = get_l2obj(op->l2s)) == NULL){
			return;
		}
		add_l2_to_iface(is,news);
		l2host_set_opaque(op->l2s,news);
	}
	if(l2host_get_opaque(op->l2d) == NULL){
		if((newd = get_l2obj(op->l2d)) == NULL){
			return;
		}
		add_l2_to_iface(is,newd);
		l2host_set_opaque(op->l2d,newd);
	}
	resize_iface(i,is);
}

static inline void
packet_cb_locked(const interface *i,iface_state *is,omphalos_packet *op){
	if(is){
		struct timeval tdiff;
		unsigned long udiff;

		handle_l2(i,is,op);
		timersub(&i->lastseen,&is->lastprinted,&tdiff);
		udiff = timerusec(&tdiff);
		if(udiff < 500000){ // At most one update every 1/2s
			if(!is->alarmset){
				// FIXME register the alarm
				is->alarmset = 1;
			}
			return;
		}
		is->lastprinted = i->lastseen;
		if(is == current_iface && details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		print_iface_state(i,is);
		full_screen_update();
	}
}

static void
packet_callback(void *unsafe,omphalos_packet *op){
	pthread_mutex_lock(&bfl);
	packet_cb_locked(op->i,unsafe,op);
	pthread_mutex_unlock(&bfl);
}

static iface_state *
create_interface_state(interface *i){
	iface_state *ret;
	const char *tstr;

	if( (tstr = lookup_arptype(i->arptype,NULL)) ){
		if( (ret = malloc(sizeof(*ret))) ){
			ret->first_visible = 0;
			ret->l2ents = 0;
			ret->l2objs = NULL;
			ret->ysize = lines_for_interface(i,ret);
			ret->alarmset = ret->devaction = 0;
			ret->typestr = tstr;
			ret->lastprinted.tv_sec = ret->lastprinted.tv_usec = 0;
			ret->iface = i;
		}
	}
	return ret;
}

static inline void *
interface_cb_locked(interface *i,iface_state *ret){
	if(ret == NULL){
		if( (ret = create_interface_state(i)) ){
			int rows,cols;

			if((ret->prev = current_iface) == NULL){
				current_iface = ret->prev = ret->next = ret;
				ret->scrline = START_LINE;
			}else{
				// The order on screen must match the list order, so splice it onto
				// the end. We might be anywhere, so use absolute coords (scrline).
				while(ret->prev->next->scrline > ret->prev->scrline){
					ret->prev = ret->prev->next;
				}
				ret->scrline = lines_for_interface(ret->prev->iface,ret->prev)
					+ ret->prev->scrline + 1;
				ret->next = ret->prev->next;
				ret->next->prev = ret;
				ret->prev->next = ret;
			}
			getmaxyx(stdscr,rows,cols);
			// FIXME check rows; might need kick an iface
			// off or start hidden.
			assert(rows); // FIXME
			if( (ret->subwin = newwin(ret->ysize,PAD_COLS(cols),ret->scrline,START_COL)) &&
					(ret->panel = new_panel(ret->subwin)) ){
				// Want the subdisplay left above this
				// new iface, should they intersect.
				assert(bottom_panel(ret->panel) == OK);
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
	}else{ // preexisting interface might need be expanded / collapsed
		resize_iface(i,ret);
	}
	if(ret){
		if(ret == current_iface && details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		iface_box(ret->subwin,i,ret);
		if(i->flags & IFF_UP){
			print_iface_state(i,ret);
			if(ret->devaction < 0){
				wstatus_locked(pad,"");
				ret->devaction = 0;
			}
			full_screen_update();
		}else if(ret->devaction > 0){
			wstatus_locked(pad,"");
			ret->devaction = 0;
			full_screen_update();
		}
	}
	return ret;
}

static void *
interface_callback(interface *i,void *unsafe){
	void *r;

	pthread_mutex_lock(&bfl);
	r = interface_cb_locked(i,unsafe);
	pthread_mutex_unlock(&bfl);
	return r;
}

static void *
wireless_callback(interface *i,unsigned wcmd __attribute__ ((unused)),void *unsafe){
	void *r;

	pthread_mutex_lock(&bfl);
	r = interface_cb_locked(i,unsafe);
	wstatus_locked(pad,"wireless event on %s",i->name); // FIXME
	pthread_mutex_unlock(&bfl);
	return r;
}

static inline void
interface_removed_locked(iface_state *is){
	if(is){
		l2obj *l = is->l2objs;

		while(l){
			l2obj *tmp = l->next;
			free(l);
			l = tmp;
		}
		werase(is->subwin);
		del_panel(is->panel);
		delwin(is->subwin);
		if(is->next != is){
			is->next->prev = is->prev;
			is->prev->next = is->next;
			if(is == current_iface){
				current_iface = is->prev;
			}
			// If we owned the details window, give it to the new
			// current_iface.
			if(details.p){
				iface_details(panel_window(details.p),get_current_iface(),details.ysize);
			}
		}else{
			// If we owned the details window, destroy it FIXME
			current_iface = NULL;
		}
		free(is);
		// FIXME need move other ifaces up
		full_screen_update();
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
	if((pad = ncurses_setup(&pctx.iface)) == NULL){
		return EXIT_FAILURE;
	}
	if(omphalos_init(&pctx)){
		int err = errno;

		mandatory_cleanup(&pad);
		fprintf(stderr,"Error in omphalos_init() (%s?)\n",strerror(err));
		return EXIT_FAILURE;
	}
	omphalos_cleanup(&pctx);
	if(mandatory_cleanup(&pad)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
