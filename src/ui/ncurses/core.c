#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/iface.h>
#include <omphalos/ethtool.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static unsigned count_interface;
static struct iface_state *current_iface;

// Status bar at the bottom of the screen. Must be reallocated upon screen
// resize and allocated based on initial screen at startup. Don't shrink
// it; widening the window again should show the full message.
static char *statusmsg;
static int statuschars;	// True size, not necessarily what's available

static const interface *
get_current_iface(void){
	if(current_iface){
		return current_iface->iface;
	}
	return NULL;
}

int wvstatus_locked(WINDOW *w,const char *fmt,va_list va){
	assert(statuschars > 0);
	if(fmt == NULL){
		statusmsg[0] = '\0';
	}else{
		vsnprintf(statusmsg,statuschars,fmt,va);
	}
	return draw_main_window(w);
}

// NULL fmt clears the status bar
int wstatus_locked(WINDOW *w,const char *fmt,...){
	va_list va;
	int ret;

	va_start(va,fmt);
	ret = wvstatus_locked(w,fmt,va);
	va_end(va);
	return ret;
}

static inline int
iface_lines_bounded(const interface *i,const struct iface_state *is,int rows){
	int lines = lines_for_interface(is);

	if(lines > rows - 2){ // top and bottom border
		lines = rows - 2;
		if(lines < 2 + interface_up_p(i)){
			lines = 2 + interface_up_p(i);
		}
	}
	return lines;
}

static inline int
iface_lines_unbounded(const interface *i,const struct iface_state *is){
	return iface_lines_bounded(i,is,INT_MAX);
}

static inline void
iface_box_generic(const interface *i,const struct iface_state *is){
	iface_box(i,is,is == current_iface);
}

static inline void
redraw_iface_generic(const struct iface_state *is){
	redraw_iface(is,is == current_iface);
}

static inline int
move_interface_generic(struct iface_state *is,int rows,int delta){
	return move_interface(is,rows,delta,is == current_iface);
}

static int
offload_details(WINDOW *w,const interface *i,int row,int col,const char *name,
						unsigned val){
	int r;

	r = iface_offloaded_p(i,val);
	return mvwprintw(w,row,col,"%s%c",name,r > 0 ? '+' : r < 0 ? '?' : '-');
}

// Create a panel at the bottom of the window, referred to as the "subdisplay".
// Only one can currently be active at a time. Window decoration and placement
// is managed here; only the rows needed for display ought be provided.
int new_display_panel(WINDOW *w,struct panel_state *ps,int rows,int cols,const wchar_t *hstr){
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
	assert(bevel(psw,0) == OK);
	assert(wattroff(psw,A_BOLD) != ERR);
	assert(wcolor_set(psw,PHEADING_COLOR,NULL) == OK);
	assert(mvwaddwstr(psw,0,START_COL * 2,hstr) != ERR);
	assert(mvwaddwstr(psw,rows + 1,cols - (crightlen + START_COL * 2),crightstr) != ERR);
	assert(wcolor_set(psw,BULKTEXT_COLOR,NULL) == OK);
	return OK;
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
			char mac[i->addrlen * 3];

			l2ntop(i->addr,i->addrlen,mac);
			assert(mvwprintw(hw,row + z,col,"%-16s %-*s",i->name,scrcols - (START_COL * 4 + IFNAMSIZ + 1),mac) != ERR);
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

// An interface (pusher) has had its bottom border moved up or down (positive or
// negative delta, respectively). Update the interfaces below it on the screen
// (all those up until those actually displayed above it on the screen). Should
// be called before pusher->scrline has been updated.
static int
push_interfaces_below(iface_state *pusher,int rows,int delta){
	iface_state *is = pusher->next;

	
	while(is != pusher && is->scrline >= pusher->scrline){
		is = is->next;
	}
	while((is = is->prev) != pusher){
		int i;

		if( (i = move_interface_generic(is,rows,delta)) ){
			return i;
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
	iface_state *is = pusher->prev;


	while(is != pusher && is->scrline + iface_lines_bounded(is->iface,is,rows) <= pusher->scrline + iface_lines_bounded(pusher->iface,pusher,rows)){
		is = is->prev;
	}
	while((is = is->next) != pusher){
		int i;

		if( (i = move_interface_generic(is,rows,delta)) ){
			return i;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME
	/* while(is->scrline < 0){
		is = is->next;
	}*/
	return OK;
}

// Upon entry, the display might not have been updated to reflect a change in
// the interface's data. If so, the interface panel is resized (subject to the
// containing window's constraints) and other panels are moved as necessary.
// The interface's display is synchronized via redraw_iface() whether a resize
// is performed or not (unless it's invisible).
static int
resize_iface(const interface *i,iface_state *is){
	const int nlines = iface_lines_unbounded(i,is);
	int rows,cols,subrows,subcols;

	if(panel_hidden(is->panel)){ // resize upon becoming visible
		return OK;
	}
	getmaxyx(stdscr,rows,cols);
	getmaxyx(is->subwin,subrows,subcols);
	assert(subcols); // FIXME
	if(nlines != subrows){
		// FIXME don't allow interfaces other than those selected to
		// push other interfaces offscreen, or at least the selected ones
		// on grow down, check if we're selected; if not, check to
		// ensure none get pushed off down. vice versa.
		if(nlines < subrows){
			assert(werase(is->subwin) == OK);
			screen_update();
			assert(wresize(is->subwin,nlines,PAD_COLS(cols)) != ERR);
			assert(replace_panel(is->panel,is->subwin) != ERR);
			// FIXME when we collapse, we need pull up other
			// interfaces to fill the vacated space
		}else{
			// Try to expand down first. If that won't work, expand up.
			if(nlines + is->scrline < rows){
				int delta = nlines - subrows;

				if(push_interfaces_below(is,rows,delta)){
					return OK;
				}
				assert(wresize(is->subwin,nlines,PAD_COLS(cols)) != ERR);
				assert(replace_panel(is->panel,is->subwin) != ERR);
			}else if(is->scrline != 1){
				int delta = nlines - subrows;

				if(delta > is->scrline - 1){
					delta = is->scrline - 1;
				}
				is->scrline -= delta;
				if(push_interfaces_above(is,rows,-delta)){
					is->scrline += delta;
					return OK;
				}
				assert(move_panel(is->panel,is->scrline,1) != ERR);
				assert(wresize(is->subwin,iface_lines_bounded(i,is,rows),PAD_COLS(cols)) != ERR);
				assert(replace_panel(is->panel,is->subwin) != ERR);
			}
		}
	}
	redraw_iface_generic(is);
	return OK;
}

// Pass current number of columns
int setup_statusbar(int cols){
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

#define NETWORKROWS 1	// FIXME

int display_network_locked(WINDOW *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,NETWORKROWS,0,L"press 'n' to dismiss display")){
		goto err;
	}
	// FIXME display network info
	return OK;

err:
	if(ps->p){
		WINDOW *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return ERR;
}

int display_details_locked(WINDOW *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,DETAILROWS,0,L"press 'v' to dismiss details")){
		goto err;
	}
	if(current_iface){
		if(iface_details(panel_window(ps->p),current_iface->iface,ps->ysize)){
			goto err;
		}
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
	return ERR;
}

void toggle_promisc_locked(const omphalos_iface *octx,WINDOW *w){
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

void sniff_interface_locked(const omphalos_iface *octx,WINDOW *w){
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

void down_interface_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(interface_up_p(i)){
			wstatus_locked(w,"Bringing down %s...",i->name);
			current_iface->devaction = 1;
			down_interface(octx,i);
		}
	}
}

void hide_panel_locked(struct panel_state *ps){
	if(ps){
		WINDOW *psw;

		psw = panel_window(ps->p);
		hide_panel(ps->p);
		del_panel(ps->p);
		ps->p = NULL;
		delwin(psw);
		ps->ysize = -1;
	}
}

int packet_cb_locked(const interface *i,omphalos_packet *op,struct panel_state *ps){
	iface_state *is = op->i->opaque;

	if(is){
		struct timeval tdiff;
		unsigned long udiff;

		timersub(&op->tv,&is->lastprinted,&tdiff);
		udiff = timerusec(&tdiff);
		if(udiff < 500000){ // At most one update every 1/2s
			return 0;
		}
		is->lastprinted = op->tv;
		if(is == current_iface && ps->p){
			iface_details(panel_window(ps->p),i,ps->ysize);
		}
		redraw_iface_generic(is);
		return 1;
	}
	return 0;
}

void *interface_cb_locked(interface *i,iface_state *ret,struct panel_state *ps){
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
				ret->scrline = iface_lines_bounded(ret->prev->iface,ret->prev,rows) + ret->prev->scrline + 1;
			}
			// we're not yet in the list -- nothing points to us --
			// though ret->prev is valid.
			if((ret->subwin = newwin(lines_for_interface(ret),PAD_COLS(cols),ret->scrline,START_COL)) == NULL ||
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
			if(!iface_wholly_visible_p(rows,ret)){
				assert(hide_panel(ret->panel) != ERR);
			}
			draw_main_window(stdscr); // update iface count
		}
	}
	if(ret == current_iface && ps->p){
		iface_details(panel_window(ps->p),i,ps->ysize);
	}
	resize_iface(i,ret);
	if(interface_up_p(i)){
		if(ret->devaction < 0){
			wstatus_locked(stdscr,"");
			ret->devaction = 0;
		}
	}else if(ret->devaction > 0){
		wstatus_locked(stdscr,"");
		ret->devaction = 0;
	}
	return ret; // callers are responsible for screen_update()
}

void interface_removed_locked(iface_state *is,struct panel_state *ps){
	if(is){
		const int visible = !panel_hidden(is->panel);
		int rows,cols;

		free_iface_state(is);
		--count_interface;
		wclear(is->subwin);
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
				if(ps->p){
					iface_details(panel_window(ps->p),get_current_iface(),ps->ysize);
				}
			}
			getmaxyx(stdscr,scrrows,scrcols);
			assert(scrcols);
			assert(cols);
			if(visible){
				for(ci = is->next ; ci->scrline > is->scrline ; ci = ci->next){
					move_interface_generic(ci,scrrows,-(rows + 1));
				}
			}
		}else{
			// If details window exists, destroy it FIXME
			current_iface = NULL;
		}
		// Calls draw_main_window(), which will update iface count
		wstatus_locked(stdscr,"%s went away",is->iface->name);
		free(is);
	}
}

struct l2obj *neighbor_callback_locked(const interface *i,struct l2host *l2){
	struct l2obj *ret;
	iface_state *is;

	// Guaranteed by callback properties -- we don't get neighbor callbacks
	// until there's been a successful device callback.
	// FIXME experimental work on reordering callbacks
	if(i->opaque == NULL){
		return NULL;
	}
	is = i->opaque;
	if((ret = l2host_get_opaque(l2)) == NULL){
		if((ret = add_l2_to_iface(i,is,l2)) == NULL){
			return NULL;
		}
	}
	resize_iface(i,is);
	return ret;
}

struct l3obj *host_callback_locked(const interface *i,struct l2host *l2,struct l3host *l3){
	struct l2obj *l2o;
	struct l3obj *ret;
	iface_state *is;

	if(((is = i->opaque) == NULL) || !l2){
		return NULL;
	}
	if((l2o = l2host_get_opaque(l2)) == NULL){
		return NULL;
	}
	if((ret = l3host_get_opaque(l3)) == NULL){
		if((ret = add_l3_to_iface(is,l2o,l3)) == NULL){
			return NULL;
		}
	}
	resize_iface(i,is);
	return ret;
}

// to be called only while ncurses lock is held
int draw_main_window(WINDOW *w){
	char hostname[HOST_NAME_MAX + 1];
	int rows,cols,scol;

	getmaxyx(w,rows,cols);
	if(gethostname(hostname,sizeof(hostname))){
		goto err;
	}
	// POSIX.1-2001 doesn't guarantee a terminating null on truncation
	hostname[sizeof(hostname) - 1] = '\0';
	assert(wattrset(w,A_DIM | COLOR_PAIR(BORDER_COLOR)) != ERR);
	if(bevel(w,0) != OK){
		goto err;
	}
	if(setup_statusbar(cols)){
		goto err;
	}
	// FIXME move this over! it is ugly on the left, clashing with ifaces
	// 5 for 0-offset, '[', ']', and 2 spaces on right side.
	// 5 for '|', space before and after, and %2d-formatted integer
	scol = cols - 5 - __builtin_strlen(PROGNAME) - 1 - __builtin_strlen(VERSION)
		- 1 - __builtin_strlen("on") - 1 - strlen(hostname)
		- 5 - __builtin_strlen("iface" - (count_interface != 1));
	assert(mvwprintw(w,0,scol,"[") != ERR);
	assert(wattron(w,A_BOLD | COLOR_PAIR(HEADER_COLOR)) != ERR);
	assert(wprintw(w,"%s %s on %s | %d iface%s",PROGNAME,VERSION,
			hostname,count_interface,count_interface == 1 ? "" : "s") != ERR);
	assert(wattrset(w,COLOR_PAIR(BORDER_COLOR)) != ERR);
	assert(wprintw(w,"]") != ERR);
	assert(wattron(w,A_BOLD | COLOR_PAIR(FOOTER_COLOR)) != ERR);
	// addstr() doesn't interpret format strings, so this is safe. It will
	// fail, however, if the string can't fit on the window, which will for
	// instance happen if there's an embedded newline.
	assert(mvwaddstr(w,rows - 1,START_COL * 2,statusmsg) != ERR);
	assert(wattroff(w,A_BOLD | COLOR_PAIR(FOOTER_COLOR)) != ERR);
	return OK;

err:
	return ERR;
}

static void
reset_interface_stats(WINDOW *w,const interface *i){
	unimplemented(w,i);
}

void reset_all_interface_stats(WINDOW *w){
	iface_state *is;

	if( (is = current_iface) ){
		do{
			const interface *i = get_current_iface(); // FIXME get_iface(is);

			reset_interface_stats(w,i);
		}while((is = is->next) != current_iface);
	}
}

void reset_current_interface_stats(WINDOW *w){
	const interface *i;

	if( (i = get_current_iface()) ){
		reset_interface_stats(w,i);
	}
}

void use_next_iface_locked(WINDOW *w,struct panel_state *ps){
	if(current_iface && current_iface->next != current_iface){
		iface_state *is,*oldis = current_iface;
		int rows,cols;
		interface *i;

		getmaxyx(w,rows,cols);
		assert(cols);
		is = current_iface = current_iface->next;
		i = is->iface;
		if(panel_hidden(is->panel)){
			int up;

			is->scrline = rows - iface_lines_bounded(i,is,rows) - 1;
			up = oldis->scrline + iface_lines_bounded(oldis->iface,oldis,rows) + 1 - is->scrline;
			if(up > 0){
				push_interfaces_above(is,rows,-up);
			}
			assert(wresize(is->subwin,iface_lines_bounded(i,is,rows),PAD_COLS(cols)) != ERR);
			assert(replace_panel(is->panel,is->subwin) != ERR);
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			redraw_iface_generic(is);
			assert(show_panel(is->panel) != ERR);
		}else if(is->scrline < oldis->scrline){
			is->scrline = oldis->scrline + (iface_lines_bounded(oldis->iface,oldis,rows) - iface_lines_bounded(i,is,rows));
			push_interfaces_above(is,rows,-(iface_lines_bounded(i,is,rows) + 1));
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			redraw_iface_generic(is);
		}else{
			iface_box_generic(oldis->iface,oldis);
			resize_iface(i,is);
		}
		if(panel_hidden(oldis->panel)){
			// we hid the entire panel, and thus might have space
			// to move up into. move as many interfaces as we can
			// onscreen FIXME
		}
		if(ps->p){
			assert(top_panel(ps->p) != ERR);
			iface_details(panel_window(ps->p),i,ps->ysize);
		}
	}
}

void use_prev_iface_locked(WINDOW *w,struct panel_state *ps){
	if(current_iface && current_iface->prev != current_iface){
		iface_state *is,*oldis = current_iface;
		int rows,cols;
		interface *i;

		getmaxyx(w,rows,cols);
		assert(cols);
		is = current_iface = current_iface->prev;
		i = is->iface;
		if(panel_hidden(is->panel)){
			int shift;

			is->scrline = 1;
			shift = iface_lines_bounded(i,is,rows) + 1 - (oldis->scrline - 1);
			if(iface_lines_bounded(i,is,rows) != iface_lines_unbounded(i,is)){
				--shift; // no blank line will follow
			}
			push_interfaces_below(is,rows,shift);
			assert(wresize(is->subwin,iface_lines_bounded(i,is,rows),PAD_COLS(cols)) != ERR);
			assert(replace_panel(is->panel,is->subwin) != ERR);
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			redraw_iface_generic(is);
			assert(show_panel(is->panel) != ERR);
		}else if(is->scrline > oldis->scrline){
			is->scrline = 1;
			push_interfaces_below(is,rows,iface_lines_bounded(i,is,rows) + 1);
			assert(move_panel(is->panel,is->scrline,START_COL) != ERR);
			redraw_iface_generic(is);
		}else{
			iface_box_generic(oldis->iface,oldis);
			resize_iface(i,is);
		}
		if(panel_hidden(oldis->panel)){
			// we hid the entire panel, and thus might have space
			// to move down into. move as many interfaces as we can
			// onscreen FIXME
		}
		if(ps->p){
			assert(top_panel(ps->p) != ERR);
			iface_details(panel_window(ps->p),i,ps->ysize);
		}
	}
}

int expand_iface_locked(void){
	if(!current_iface){
		return 0;
	}
	expand_interface(current_iface);
	return resize_iface(current_iface->iface,current_iface);
}

int collapse_iface_locked(void){
	if(!current_iface){
		return 0;
	}
	collapse_interface(current_iface);
	return resize_iface(current_iface->iface,current_iface);
}
