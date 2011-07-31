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
#include <omphalos/timing.h>
#include <ncursesw/ncurses.h>
#include <omphalos/hwaddrs.h>
#include <ui/ncurses/iface.h>
#include <gnu/libc-version.h>
#include <omphalos/ethtool.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

#define ERREXIT endwin() ; fprintf(stderr,"ncurses failure|%s|%d\n",__func__,__LINE__); abort() ; goto err

// Add ((format (printf))) attributes to ncurses functions, which sadly
// lack them (at least as of Debian's 5.9-1).
extern int wprintw(WINDOW *,const char *,...) __attribute__ ((format (printf,2,3)));
extern int mvwprintw(WINDOW *,int,int,const char *,...) __attribute__ ((format (printf,4,5)));

#define PROGNAME "omphalos"	// FIXME
#define VERSION  "0.98-pre"	// FIXME

#define PAD_LINES 3
#define PAD_COLS(cols) ((cols) - START_COL * 2)
#define START_LINE 1
#define START_COL 1
#define PREFIXSTRLEN U64STRLEN

// FIXME we ought precreate the subwindows, and show/hide them rather than
// creating and destroying them every time.
struct panel_state {
	PANEL *p;
	int ysize;			// number of lines of *text* (not win)
};

#define PANEL_STATE_INITIALIZER { .p = NULL, .ysize = -1, }

static struct panel_state *active;
static struct panel_state help = PANEL_STATE_INITIALIZER;
static struct panel_state details = PANEL_STATE_INITIALIZER;

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
iface_box_generic(WINDOW *w,const interface *i,const iface_state *is){
	return iface_box(w,i,is,is == current_iface);
}

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
screen_update(void){
	int ret;

	assert((ret = start_screen_update()) == 0);
	assert((ret |= finish_screen_update()) == 0);
	return ret;
}

// Is the interface window entirely visible? We can't draw it otherwise, as it
// will obliterate the global bounding box.
static int
iface_visible_p(int rows,const iface_state *is){
	if(is->scrline + is->ysize >= rows){
		return 0;
	}else if(is->scrline < START_LINE){
		return 0;
	}
	return 1;
}

static int
iface_will_be_visible_p(int rows,const iface_state *ret,int nlines){
	if(ret->scrline + nlines >= rows){
		return 0;
	}else if(ret->scrline < START_LINE){
		return 0;
	}
	return 1;
}

// This is the number of lines we'd have in an optimal world; we might have
// fewer available to us on this screen at this time. ->ysize is real size.
static inline int
lines_for_interface(const interface *i,const iface_state *is){
	return PAD_LINES + is->l2ents - !interface_up_p(i);
}

static int
redraw_iface(const interface *i,struct iface_state *is){
	assert(werase(is->subwin) != ERR);
	if(iface_box_generic(is->subwin,i,is) == ERR){
		return ERR;
	}
	if(interface_up_p(i)){
		if(print_iface_state(i,is)){
			return ERR;
		}
	}
	if(print_iface_hosts(i,is)){
		return ERR;
	}
	return OK;
}

// Move this interface, possibly hiding it or bringing it onscreen. Negative
// delta indicates movement up, positive delta moves down.
static int
move_interface(iface_state *is,int rows,int delta){
	is->scrline += delta;
	if(iface_visible_p(rows,is)){
		interface *ii = is->iface;

		assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
		if(redraw_iface(ii,is)){
			return ERR;
		}
	// use "will_be_visible" as "would_be_visible" here, heh
	}else if(iface_will_be_visible_p(rows,is,is->ysize - delta)){
		// FIXME see if we can shrink it first!
		assert(werase(is->subwin) != ERR);
		assert(hide_panel(is->panel) != ERR);
	}
	return OK;
}

// An interface (pusher) has had its bottom border moved up or down (positive or
// negative delta, respectively). Update the interfaces below it on the screen
// (all those up until those actually displayed above it on the screen). Should
// be called before pusher->scrline has been updated.
static int
push_interfaces_below(iface_state *pusher,int rows,int delta){
	iface_state *is;

	for(is = pusher->next ; is->scrline > pusher->scrline ; is = is->next){
		if(move_interface(is,rows,delta)){
			return ERR;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME
	/* while(is->scrline < 0){
		is = is->next;
	}*/
	return OK;
}

// An interface (pusher) has had its top border moved up or down (positive or
// negative delta, respectively). Update the interfaces above it on the screen
// (all those up until those actually displayed below it on the screen). Should
// be called before pusher->scrline has been updated.
static int
push_interfaces_above(iface_state *pusher,int rows,int delta){
	iface_state *is;

<<<<<<< HEAD
	for(is = pusher->prev ; is->scrline + lines_for_interface(is->iface,is) <= pusher->scrline + lines_for_interface(pusher->iface,pusher) ; is = is->prev){
		if(is == pusher){
			break;
		}
=======
	for(is = pusher->prev ; is->scrline < pusher->scrline ; is = is->prev){
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
		if(move_interface(is,rows,delta)){
			return ERR;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME
	/* while(is->scrline < 0){
		is = is->next;
	}*/
	return OK;
}

// Upon entry, ret->ysize (and the actual display) might not have been updated
// to reflect a change in the interface's data. If so, the interface panel is
// resized (subject to the containing window's constraints) and other panels
// are moved as necessary. The interface's display is synchronized via
// redraw_iface() whether a resize is performed or not (unless it's invisible).
static int
resize_iface(const interface *i,iface_state *ret){
	const int nlines = lines_for_interface(i,ret);
	int rows,cols;

	getmaxyx(stdscr,rows,cols);
	if(!iface_will_be_visible_p(rows,ret,nlines)){
		if(!iface_visible_p(rows,ret)){ // we weren't visible to begin with
			return OK;
		} // else need to erase it
	}
	if(nlines != ret->ysize){
		if(nlines + ret->scrline < rows){
			int delta = nlines - ret->ysize;

			push_interfaces_below(ret,rows,delta);
			ret->ysize = nlines;
			assert(wresize(ret->subwin,ret->ysize,PAD_COLS(cols)) != ERR);
			assert(replace_panel(ret->panel,ret->subwin) != ERR);
		}
	}
	if(redraw_iface(i,ret) == ERR){
		return ERR;
	}
	return screen_update();
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
				strftime(sm,s,"launched at %T. 'h' toggles help.",&tm);
			}else{
				sm[0] = '\0';
			}
		}
		statusmsg = sm;
	}
	return 0;
}

// to be called only while ncurses lock is held
static int
draw_main_window(WINDOW *w,const char *name,const char *ver){
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(wcolor_set(w,BORDER_COLOR,NULL) != ERR);
	if(bevel(w) != OK){
		ERREXIT;
	}
	if(setup_statusbar(cols)){
		ERREXIT;
	}
	// FIXME move this over! it is ugly on the left, clashing with ifaces
	assert(mvwprintw(w,0,2,"[") != ERR);
	assert(wattron(w,A_BOLD | COLOR_PAIR(HEADING_COLOR)) != ERR);
	assert(wprintw(w,"%s %s on %s %s (libc %s-%s)",name,ver,sysuts.sysname,
				sysuts.release,glibc_version,glibc_release) != ERR);
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
	assert(rows);
	assert(mvwaddstr(w,rows - 1,START_COL * 2,statusmsg) != ERR);
	if(wattroff(w,A_BOLD | COLOR_PAIR(BORDER_COLOR)) != OK){
		ERREXIT;
	}
	if(wcolor_set(w,0,NULL) != OK){
		ERREXIT;
	}
	return screen_update();

err:
	return -1;
}

static int
wvstatus_locked(WINDOW *w,const char *fmt,va_list va){
	assert(statuschars > 0);
	if(fmt == NULL){
		statusmsg[0] = '\0';
	}else{
		assert(vsnprintf(statusmsg,statuschars,fmt,va) < statuschars);
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
new_display_panel(WINDOW *w,struct panel_state *ps,int rows,int cols,const wchar_t *hstr){
	const wchar_t crightstr[] = L"copyright © 2011 nick black";
	const int crightlen = wcslen(crightstr);
	WINDOW *psw;
	int x,y;

	getmaxyx(w,y,x);
	if(cols == 0){
		cols = x - (START_COL * 2);
	}
	assert(y >= rows + 3);
	assert((x >= cols + START_COL * 2) && (x >= crightlen + START_COL * 2));
	assert( (psw = newwin(rows + 2,cols,
					y - (rows + 3),
					x - (START_COL + cols))) );
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
	assert(bevel(psw) == OK);
	assert(wattroff(psw,A_BOLD) != ERR);
	assert(wcolor_set(psw,PHEADING_COLOR,NULL) == OK);
	assert(mvwaddwstr(psw,0,START_COL * 2,hstr) != ERR);
	assert(mvwaddwstr(psw,rows + 1,cols - (crightlen + START_COL * 2),crightstr) != ERR);
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

#define DETAILROWS 9

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
	}case 7:{
		assert(mvwprintw(hw,row + z,col,"mform: "U64FMT" noprot: "U64FMT,
					i->malformed,i->noprotocol) != ERR);
		--z;
	}case 6:{
		assert(mvwprintw(hw,row + z,col,"Rbyte: "U64FMT" frames: "U64FMT,
					i->bytes,i->frames) != ERR);
		--z;
	}case 5:{
		char b[PREFIXSTRLEN];
		char fb[PREFIXSTRLEN];
		char buf[U64STRLEN];
		assert(mvwprintw(hw,row + z,col,"RXfd: %-4d flen: %-6u fnum: %-4s blen: %-5s bnum: %-5u rxr: %5sB",
					i->rfd,i->rtpr.tp_frame_size,
					bprefix(i->rtpr.tp_frame_nr,1,fb,sizeof(fb),1),
					bprefix(i->rtpr.tp_block_size,1,buf,sizeof(buf),1),
					i->rtpr.tp_block_nr,
					bprefix(i->rs,1,b,sizeof(b),1)) != ERR);
		--z;
	}case 4:{
		assert(mvwprintw(hw,row + z,col,"Tbyte: "U64FMT" frames: "U64FMT,
					i->txbytes,i->txframes) != ERR);
		--z;
	}case 3:{
		char b[PREFIXSTRLEN];
		char fb[PREFIXSTRLEN];
		char buf[U64STRLEN];

		assert(mvwprintw(hw,row + z,col,"TXfd: %-4d flen: %-6u fnum: %-4s blen: %-5s bnum: %-5u txr: %5sB",
					i->fd,i->ttpr.tp_frame_size,
					bprefix(i->ttpr.tp_frame_nr,1,fb,sizeof(fb),1),
					bprefix(i->ttpr.tp_block_size,1,buf,sizeof(buf),1),i->ttpr.tp_block_nr,
					bprefix(i->ts,1,b,sizeof(b),1)) != ERR);
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
		assert(mvwprintw(hw,row + z,col + 53," MTU: %-6d",i->mtu) != ERR);
		--z;
	}case 1:{
		assert(mvwprintw(hw,row + z,col,"%-*s",scrcols - 2,i->topinfo.devname ?
					i->topinfo.devname : "Unknown device") != ERR);
		--z;
	}case 0:{
		if(i->addr){
			char *mac;

			if((mac = hwaddrstr(i)) == NULL){
				return ERR;
			}
			assert(mvwprintw(hw,row + z,col,"%-16s %-*s",i->name,scrcols - (START_COL * 4 + IFNAMSIZ + 1),mac) != ERR);
			free(mac);
		}else{
			assert(mvwprintw(hw,row + z,col,"%-16s %-*s",i->name,scrcols - (START_COL * 4 + IFNAMSIZ + 1),"") != ERR);
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
	if(new_display_panel(mainw,ps,DETAILROWS,0,L"press 'v' to dismiss details")){
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
	L"'k'/'↑': previous interface   'j'/'↓': next interface",
	L"'m': change device MAC        'u': change device MTU",
	L"'r': reset interface's stats  'R': reset all interfaces' stats",
	L"'d': bring down device        'p': toggle promiscuity",
	L"'s': toggle sniffing, bringing up interface if down",
	L"'v': view interface details   'h': toggle this help display",
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
use_next_iface_locked(WINDOW *w){
	if(current_iface && current_iface->next != current_iface){
		iface_state *is,*oldis = current_iface;
		int rows,cols;
		interface *i;

		getmaxyx(w,rows,cols);
		assert(cols);
		is = current_iface = current_iface->next;
		i = is->iface;
		if(!iface_visible_p(rows,is)){
<<<<<<< HEAD
			is->scrline = rows - lines_for_interface(i,is) - 1;
			push_interfaces_above(is,rows,-(lines_for_interface(i,is) + 1));
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			assert(redraw_iface(i,is) == OK);
=======
			push_interfaces_above(is,rows,-(is->ysize + 1));
			is->scrline = rows - is->ysize;
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
			assert(show_panel(is->panel) != ERR);
		}else if(is->scrline < oldis->scrline){
			is->scrline = oldis->scrline + (lines_for_interface(oldis->iface,oldis) - lines_for_interface(i,is));
			push_interfaces_above(is,rows,-(lines_for_interface(i,is) + 1));
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			assert(redraw_iface(i,is) == OK);
		}else{
<<<<<<< HEAD
			iface_box_generic(oldis->subwin,oldis->iface,oldis);
=======
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
			iface_box_generic(is->subwin,i,is);
		}
		if(details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		screen_update();
	}
}

static void
use_prev_iface_locked(WINDOW *w){
	if(current_iface && current_iface->prev != current_iface){
		iface_state *is,*oldis = current_iface;
		int rows,cols;
		interface *i;

		getmaxyx(w,rows,cols);
		assert(cols);
<<<<<<< HEAD
		is = current_iface = current_iface->prev;
		i = is->iface;
		if(!iface_visible_p(rows,is)){
			is->scrline = 1;
			push_interfaces_below(is,rows,is->ysize + 1);
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			assert(redraw_iface(i,is) == OK);
=======
		current_iface = current_iface->prev;
		iface_box_generic(is->subwin,i,is);
		is = current_iface;
		i = is->iface;
		if(!iface_visible_p(rows,is)){
			push_interfaces_below(is,rows,is->ysize + 1);
			is->scrline = 1;
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
			assert(show_panel(is->panel) != ERR);
		}else if(is->scrline > oldis->scrline){
			is->scrline = 1;
			push_interfaces_below(is,rows,is->ysize + 1);
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			assert(redraw_iface(i,is) == OK);
		}else{
<<<<<<< HEAD
			iface_box_generic(oldis->subwin,oldis->iface,oldis);
=======
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
			iface_box_generic(is->subwin,i,is);
		}
		if(details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		screen_update();
	}
}

// Completely redraw the screen, for instance after a corruption or resize.
static void
redraw_screen_locked(WINDOW *w){
	int rows,cols;

	getmaxyx(w,rows,cols);
	assert(wresize(w,rows,cols) != ERR);
	redrawwin(w);
	draw_main_window(w,PROGNAME,VERSION);
	// FIXME need iterate over interface windows, hiding or making them
	// visible as appropriate, and possibly scrolling to keep the current
	// interface on-screen...
	if(active){
		redrawwin(panel_window(active->p));
	}
	screen_update();
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
				use_prev_iface_locked(w);
			pthread_mutex_unlock(&bfl);
			break;
		case KEY_DOWN: case 'j':
			pthread_mutex_lock(&bfl);
				use_next_iface_locked(w);
			pthread_mutex_unlock(&bfl);
			break;
		case KEY_RESIZE: case 12: // Ctrl-L FIXME
			pthread_mutex_lock(&bfl);{
				redraw_screen_locked(w);
			}pthread_mutex_unlock(&bfl);
			break;
		case 'C':
			pthread_mutex_lock(&bfl);
				configure_prefs(w);
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

<<<<<<< HEAD
	//trace(TRACE_CALLS);
=======
>>>>>>> parent of 9a37c1b... Revert "Revert "link to debug versions of ncursesw""
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
	if(scrollok(stdscr,FALSE) != OK){
		errstr = "Couldn't disable scrolling\n";
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

static inline void
packet_cb_locked(const interface *i,omphalos_packet *op){
	iface_state *is = op->i->opaque;

	if(is){
		struct timeval tdiff;
		unsigned long udiff;

		timersub(&op->tv,&is->lastprinted,&tdiff);
		udiff = timerusec(&tdiff);
		if(udiff < 500000){ // At most one update every 1/2s
			return;
		}
		is->lastprinted = op->tv;
		if(is == current_iface && details.p){
			iface_details(panel_window(details.p),i,details.ysize);
		}
		print_iface_state(i,is);
		screen_update();
	}
}

static void
packet_callback(omphalos_packet *op){
	pthread_mutex_lock(&bfl);
	packet_cb_locked(op->i,op);
	pthread_mutex_unlock(&bfl);
}

static iface_state *
create_interface_state(interface *i){
	iface_state *ret;
	const char *tstr;

	if( (tstr = lookup_arptype(i->arptype,NULL)) ){
		if( (ret = malloc(sizeof(*ret))) ){
			ret->l2ents = 0;
			ret->l2objs = NULL;
			ret->ysize = lines_for_interface(i,ret);
			ret->devaction = 0;
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

			getmaxyx(stdscr,rows,cols);
			// need find our location on the screen before newwin()
			if((ret->prev = current_iface) == NULL){
				ret->scrline = START_LINE;
			}else{
				// The order on screen must match the list order, so splice it onto
				// the end. We might be anywhere, so use absolute coords (scrline).
				while(ret->prev->next->scrline > ret->prev->scrline){
					ret->prev = ret->prev->next;
				}
				ret->scrline = lines_for_interface(ret->prev->iface,ret->prev) + ret->prev->scrline + 1;
			}
			// we're not yet in the list -- nothing points to us --
			// though ret->prev is valid.
			if((ret->subwin = newwin(ret->ysize,PAD_COLS(cols),ret->scrline,START_COL)) == NULL ||
					(ret->panel = new_panel(ret->subwin)) == NULL){
				delwin(ret->subwin);
				free(ret);
				return NULL;
			}
			if(current_iface == NULL){
				current_iface = ret->prev = ret->next = ret;
			}else{
				ret->next = ret->prev->next;
				ret->next->prev = ret;
				ret->prev->next = ret;
			}
			++count_interface;
			// Want the subdisplay left above this new iface,
			// should they intersect.
			assert(bottom_panel(ret->panel) == OK);
			if(!iface_visible_p(rows,ret)){
				assert(hide_panel(ret->panel) != ERR);
			}
		}
	}
	if(ret == current_iface && details.p){
		iface_details(panel_window(details.p),i,details.ysize);
	}
	resize_iface(i,ret);
	if(interface_up_p(i)){
		if(ret->devaction < 0){
			wstatus_locked(pad,"");
			ret->devaction = 0;
		}
	}else if(ret->devaction > 0){
		wstatus_locked(pad,"");
		ret->devaction = 0;
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
	pthread_mutex_unlock(&bfl);
	return r;
}

static inline void
interface_removed_locked(iface_state *is){
	if(is){
		int rows,cols;

		free_iface_state(is);
		werase(is->subwin);
		del_panel(is->panel);
		getmaxyx(is->subwin,rows,cols);
		delwin(is->subwin);
		if(is->next != is){
			int scrrows,scrcols;
			iface_state *ci;

			// First, splice it out of the list
			is->next->prev = is->prev;
			is->prev->next = is->next;
			if(is == current_iface){
				current_iface = is->prev;
				// give the details window to new current_iface
				if(details.p){
					iface_details(panel_window(details.p),get_current_iface(),details.ysize);
				}
			}
			getmaxyx(pad,scrrows,scrcols);
			assert(scrcols);
			assert(cols);
			for(ci = is->next ; ci->scrline > is->scrline ; ci = ci->next){
				move_interface(ci,scrrows,-(rows + 1));
			}
		}else{
			// If details window exists, destroy it FIXME
			current_iface = NULL;
		}
		free(is);
		screen_update();
	}
}

static struct l2obj *
neighbor_callback_locked(const interface *i,struct l2host *l2){
	struct l2obj *ret;
	iface_state *is;

	// Guaranteed by callback properties -- we don't get neighbor callbacks
	// until there's been a successful device callback.
	// FIXME experimental work on reordering callbacks
	if(i->opaque == NULL){
		return NULL;
	}
	assert( (is = i->opaque) );
	if((ret = l2host_get_opaque(l2)) == NULL){
		if((ret = add_l2_to_iface(i,is,l2)) == NULL){
			return NULL;
		}
	}
	assert(resize_iface(i,is) != ERR);
	return ret;
}

static void *
neighbor_callback(const interface *i,struct l2host *l2){
	void *ret;

	pthread_mutex_lock(&bfl);
	ret = neighbor_callback_locked(i,l2);
	pthread_mutex_unlock(&bfl);
	return ret;
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
	pctx.iface.neigh_event = neighbor_callback;
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
