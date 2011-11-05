#include <assert.h>
#include <unistd.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/color.h>
#include <ui/ncurses/iface.h>
#include <omphalos/ethtool.h>
#include <omphalos/service.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static unsigned count_interface;
// dequeue + single selection
static reelbox *current_iface,*top_reelbox,*last_reelbox;

// Status bar at the bottom of the screen. Must be reallocated upon screen
// resize and allocated based on initial screen at startup. Don't shrink
// it; widening the window again should show the full message.
static wchar_t *statusmsg;
static int statuschars;	// True size, not necessarily what's available

static int resize_iface(reelbox *);

// Caller needs set up: next, prev
static reelbox *
create_reelbox(iface_state *is,int rows,int scrline,int cols){
	reelbox *ret;
	int lines;

	lines = iface_lines_bounded(is,rows);
	if(lines >= rows - scrline){
		lines = rows - scrline - 1;
	}
	if( (ret = malloc(sizeof(*ret))) ){
		if(((ret->subwin = newwin(lines,PAD_COLS(cols),scrline,START_COL)) == NULL)
				|| (ret->panel = new_panel(ret->subwin)) == NULL){
			delwin(ret->subwin);
			free(ret);
			return NULL;
		}
		ret->scrline = scrline;
		ret->is = is;
		is->rb = ret;
	}
	return ret;
}

static const interface *
get_current_iface(void){
	if(current_iface){
		return current_iface->is->iface;
	}
	return NULL;
}

static inline int
top_space_p(int rows){
	if(!top_reelbox){
		return rows - 2;
	}
	return getbegy(top_reelbox->subwin) - 1;
}

// Returns the amount of space available at the bottom.
static inline int
bottom_space_p(int rows){
	if(!last_reelbox){
		return rows - 1;
	}
	if(getmaxy(last_reelbox->subwin) + getbegy(last_reelbox->subwin) >= rows - 2){
		return 0;
	}
	return (rows - 1) - (getmaxy(last_reelbox->subwin) + getbegy(last_reelbox->subwin));
}

int wvstatus_locked(WINDOW *w,const wchar_t *fmt,va_list va){
	assert(statuschars > 0);
	if(fmt == NULL){
		statusmsg[0] = '\0';
	}else{
		vswprintf(statusmsg,statuschars,fmt,va);
	}
	return draw_main_window(w);
}

// NULL fmt clears the status bar
int wstatus_locked(WINDOW *w,const wchar_t *fmt,...){
	va_list va;
	int ret;

	va_start(va,fmt);
	ret = wvstatus_locked(w,fmt,va);
	va_end(va);
	return ret;
}

static inline int
redraw_iface_generic(const reelbox *rb){
	return redraw_iface(rb->is,rb,rb == current_iface);
}

static inline void
move_interface_generic(reelbox *rb,int rows,int cols,int delta){
	move_interface(rb,rows,cols,delta,rb == current_iface);
}

static int
offload_details(WINDOW *w,const interface *i,int row,int col,const char *name,
						unsigned val){
	int r;

	r = iface_offloaded_p(i,val);
	// these checkboxes don't really look that great at small size
	//return mvwprintw(w,row,col,"%lc%s",r > 0 ? L'☑' : r < 0 ? L'?' : L'☐',name);
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
	assert(bevel(psw) == OK);
	assert(wattroff(psw,A_BOLD) != ERR);
	assert(wcolor_set(psw,PHEADING_COLOR,NULL) == OK);
	assert(mvwaddwstr(psw,0,START_COL * 2,hstr) != ERR);
	assert(mvwaddwstr(psw,rows + 1,cols - (crightlen + START_COL * 2),crightstr) != ERR);
	assert(wcolor_set(psw,BULKTEXT_COLOR,NULL) == OK);
	return OK;
}

#define DETAILROWS 9

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
		assert(mvwprintw(hw,row + z,col,"Tbyte: "U64FMT" frames: "U64FMT" aborts: %llu",
					i->txbytes,i->txframes,i->txaborts) != ERR);
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

static void
free_reelbox(reelbox *rb){
	if(rb){
		assert(rb->is);
		assert(rb->is->rb == rb);

		rb->is->rb = NULL;
		assert(delwin(rb->subwin) == OK);
		assert(del_panel(rb->panel) == OK);
		free(rb);
	}
}

// Pull the interfaces above the puller down to fill unused space. Move from
// the puller out, as we might need make visible some unknown number of
// interfaces (and the space has already been made).
//
// If the puller is being removed, it ought already have been spliced out of
// the reelbox list, and all reelbox state updated, but it obviously must not
// yet have been freed. Its ->is pointer must still be valid (though
// ->is->iface is no longer valid). Its ->next and ->prev pointers ought not
// have been altered.
static void
pull_interfaces_down(reelbox *puller,int rows,int cols,int delta){
	reelbox *rb;

	//fprintf(stderr,"pulling down %d from %s@%d\n",delta,puller ? puller->is ? puller->is->iface->name : "destroyed" : "all",
	//		puller ? puller->scrline : rows);
	assert(delta > 0);
	rb = puller ? puller->prev : last_reelbox;
	while(rb){
		if(iface_lines_bounded(rb->is,rows) > getmaxy(rb->subwin)){
			assert(rb == top_reelbox);
			resize_iface(rb);
		}else{
			rb->scrline += delta;
			move_interface_generic(rb,rows,cols,delta);
		}
		if(panel_hidden(rb->panel)){
			//fprintf(stderr,"PULLED THE BOTTOM OFF\n");
			if((last_reelbox = rb->prev) == NULL){
				top_reelbox = NULL;
			}else{
				last_reelbox->next = NULL;
			}
			free_reelbox(rb);
			rb = last_reelbox;
		}else{
			rb = rb->prev;
		}
	}
	if(top_reelbox){
		if(top_reelbox->scrline <= 2){
			return;
		}
	}
	// FIXME make more visible
}

// Pass a NULL puller to move all interfaces up
static void
pull_interfaces_up(reelbox *puller,int rows,int cols,int delta){
	reelbox *rb;

	//fprintf(stderr,"pulling up %d from %s@%d\n",delta,puller ? puller->is ? puller->is->iface->name : "destroyed" : "all",
	//		puller ? puller->scrline : rows);
	assert(delta > 0);
	rb = puller ? puller->next : top_reelbox;
	while(rb){
		// FIXME entirely broken for partials
		rb->scrline -= delta;
		move_interface_generic(rb,rows,cols,-delta);
		if(panel_hidden(rb->panel)){
			//fprintf(stderr,"PULLED THE TOP OFF\n");
			if((top_reelbox = rb->next) == NULL){
				last_reelbox = NULL;
			}else{
				top_reelbox->prev = NULL;
			}
			free_reelbox(rb);
			rb = top_reelbox;
		}else{
			rb = rb->next;
		}
	}
	while(last_reelbox){
		struct iface_state *i;
		int scrline;

		if((scrline = last_reelbox->scrline + getmaxy(last_reelbox->subwin)) >= rows - 2){
			return;
		}
		i = last_reelbox->is->next;
		if(i->rb){
			return; // next interface is already visible
		}
		if((rb = create_reelbox(i,rows,scrline + 1,cols)) == NULL){
			return;
		}
		rb->prev = last_reelbox;
		last_reelbox->next = rb;
		rb->next = NULL;
		last_reelbox = rb;
		redraw_iface_generic(rb);
	}
}

// An interface (pusher) has had its bottom border moved up or down (positive or
// negative delta, respectively). Update the interfaces below it on the screen
// (all those up until those actually displayed above it on the screen). Should
// be called before pusher->scrline has been updated.
static void
push_interfaces_below(reelbox *pusher,int rows,int cols,int delta){
	reelbox *rb;

	assert(delta > 0);
	//fprintf(stderr,"pushing down %d from %s@%d\n",delta,pusher ? pusher->is ? pusher->is->iface->name : "destroyed" : "all",
	//		pusher ? pusher->scrline : 0);
	rb = last_reelbox;
	while(rb){
		if(rb == pusher){
			break;
		}
		rb->scrline += delta;
		move_interface_generic(rb,rows,cols,delta);
		if(panel_hidden(rb->panel)){
			if((last_reelbox = rb->prev) == NULL){
				top_reelbox = NULL;
			}else{
				last_reelbox->next = NULL;
			}
			free_reelbox(rb);
			rb = last_reelbox;
		}else{
			rb = rb->prev;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME pull_interfaces_down();
}

// An interface (pusher) has had its top border moved up or down (positive or
// negative delta, respectively). Update the interfaces above it on the screen
// (all those up until those actually displayed below it on the screen).
//
// If an interface is being brought onto the bottom of the screen, ensure that
// last_reelbox has been updated to point to it, and top_reelbox has been
// updated if the interface came from the top of the screen. Any other updates
// to reelboxes will be made by this function. Otherwise....
//
//     Update before: pusher->scrline, current_interface
//     Updated after: reelbox pointers, affected scrlines.
//
static void
push_interfaces_above(reelbox *pusher,int rows,int cols,int delta){
	reelbox *rb;

	assert(delta < 0);
	//fprintf(stderr,"pushing up %d from %s@%d\n",delta,pusher ? pusher->is ? pusher->is->iface->name : "destroyed" : "all",
	//		pusher ? pusher->scrline : rows);
	rb = top_reelbox;
	while(rb){
		if(rb == pusher){
			break;
		}
		rb->scrline += delta;
		move_interface_generic(rb,rows,cols,delta);
		if(panel_hidden(rb->panel)){
			if((top_reelbox = rb->next) == NULL){
				last_reelbox = NULL;
			}else{
				top_reelbox->prev = NULL;
			}
			free_reelbox(rb);
			rb = top_reelbox;
		}else{
			rb = rb->next;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME pull_interfaces_up(pusher,rows,cols,delta);
}

static inline int
gap_above(reelbox *rb){
	if(!rb->prev){
		return 0;
	}
	return getbegy(rb->subwin) - (getmaxy(rb->prev->subwin) + getbegy(rb->prev->subwin)) - 1;
}

static inline int
gap_below(reelbox *rb){
	if(!rb->next){
		return 0;
	}
	return getbegy(rb->next->subwin) - (getmaxy(rb->subwin) + getbegy(rb->subwin)) - 1;
}

// Upon entry, the display might not have been updated to reflect a change in
// the interface's data. If so, the interface panel is resized (subject to the
// containing window's constraints) and other panels are moved as necessary.
// The interface's display is synchronized via redraw_iface() whether a resize
// is performed or not (unless it's invisible). The display ought be partially
// visible -- ie, if we ought be invisible, we ought be already and this is not
// going to make us so. We do not redraw -- that's the callers job (we
// can't redraw, since we might not yet have been moved).
static int
resize_iface(reelbox *rb){
	const interface *i,*curi = get_current_iface();
	int rows,cols,subrows,subcols;
	iface_state *is;

	assert(rb && rb->is);
	i = rb->is->iface;
	assert(i);
	if(panel_hidden(rb->panel)){ // resize upon becoming visible
		return OK;
	}
	is = rb->is;
	getmaxyx(stdscr,rows,cols);
	const int nlines = iface_lines_bounded(is,rows);
	//fprintf(stderr,"resizing %s@%d from %d to %d\n",is->iface->name,rb->scrline,getmaxy(rb->subwin),nlines);
	getmaxyx(rb->subwin,subrows,subcols);
	assert(subcols); // FIXME
	if(nlines < subrows){ // Shrink the interface
		assert(werase(rb->subwin) == OK);
		// Without screen_update(), the werase() doesn't take effect,
		// even if wclear() is used.
		screen_update();
		assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) != ERR);
		assert(replace_panel(rb->panel,rb->subwin) != ERR);
		if(rb->scrline < current_iface->scrline){
			rb->scrline += subrows - nlines;
			assert(move_panel(rb->panel,rb->scrline,1) != ERR);
			pull_interfaces_down(rb,rows,cols,subrows - nlines);
		}else{
			pull_interfaces_up(rb,rows,cols,subrows - nlines);
		}
		return OK;
	}else if(nlines == subrows){ // otherwise, expansion
		return OK;
	}
	// The current interface grows in both directions and never becomes a
	// partial interface. We don't try to make it one here, and
	// move_interface() will refuse to perform a move resulting in one.
	if(i == curi){
		// We can't already occupy the screen, or the nlines == subrows
		// check would have thrown us out. There *is* space to grow.
		if(rb->scrline + subrows < rows - 1){ // can we grow down?
			int delta = (rows - 1) - (rb->scrline + subrows);

			if(delta + subrows > nlines){
				delta = nlines - subrows;
			}
			push_interfaces_below(rb,rows,cols,delta);
			subrows += delta;
		}
		if(nlines > subrows){ // can we grow up?
			int delta = rb->scrline - 1;

			if(delta + subrows > nlines){
				delta = nlines - subrows;
			}
			delta = -delta;
			rb->scrline += delta;
			push_interfaces_above(rb,rows,cols,delta);
			// assert(move_interface_generic(is,rows,cols,-delta) == OK);
			assert(move_panel(rb->panel,rb->scrline,1) != ERR);
		}
		assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) != ERR);
		assert(replace_panel(rb->panel,rb->subwin) != ERR);
	}else{ // we're not the current interface
		int delta;

		if( (delta = bottom_space_p(rows)) ){ // always occupy free rows
			if(delta > nlines - subrows){
				delta = nlines - subrows;
			}
			delta -= gap_below(rb); // FIXME questionable
			push_interfaces_below(rb,rows,cols,delta);
			subrows += delta;
		}
		if(nlines > subrows){
			if(rb->scrline > current_iface->scrline){ // only down
				delta = (rows - 1) - (rb->scrline + subrows);
				if(delta > nlines - subrows){
					delta = nlines - subrows;
				}
				delta -= gap_below(rb);
				if(delta > 0){
					push_interfaces_below(rb,rows,cols,delta);
				}
			}else{ // only up
				delta = rb->scrline - 1;
				if(delta > nlines - subrows){
					delta = nlines - subrows;
				}
				delta -= gap_above(rb);
				if(delta){
					push_interfaces_above(rb,rows,cols,-delta);
					rb->scrline -= delta;
					move_interface_generic(rb,rows,cols,-delta);
				}
			}
			subrows += delta;
			if(nlines > subrows){
				if( (delta = gap_below(rb)) ){
					subrows += delta > (nlines - subrows) ?
						nlines - subrows : delta;
				}
			}
			if(nlines > subrows){
				if( (delta = gap_above(rb)) ){
					subrows += delta > (nlines - subrows) ?
						nlines - subrows : delta;
				}
			}
		}
		if(subrows != getmaxy(rb->subwin)){
			assert(wresize(rb->subwin,subrows,PAD_COLS(cols)) != ERR);
			assert(replace_panel(rb->panel,rb->subwin) != ERR);
		}
		redraw_iface_generic(rb);
	}
	return OK;
}

// Pass current number of columns
int setup_statusbar(int cols){
	if(cols < 0){
		return -1;
	}
	if(statuschars <= cols){
		const size_t s = cols + 1;
		wchar_t *sm;

		if((sm = realloc(statusmsg,s * sizeof(*sm))) == NULL){
			return -1;
		}
		statuschars = s * sizeof(*sm);
		if(statusmsg == NULL){
			time_t t = time(NULL);
			struct tm tm;

			if(localtime_r(&t,&tm)){
				wcsftime(sm,s,L"launched at %T. 'h' toggles help.",&tm);
			}else{
				sm[0] = '\0';
			}
		}
		statusmsg = sm;
	}
	return 0;
}

void toggle_promisc_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(interface_promisc_p(i)){
			wstatus_locked(w,L"Disabling promiscuity on %s",i->name);
			disable_promiscuity(octx,i);
		}else{
			wstatus_locked(w,L"Enabling promiscuity on %s",i->name);
			enable_promiscuity(octx,i);
		}
	}
}

void sniff_interface_locked(const omphalos_iface *octx,WINDOW *w){
	const interface *i = get_current_iface();

	if(i){
		if(!interface_sniffing_p(i)){
			if(!interface_up_p(i)){
				wstatus_locked(w,L"Bringing up %s...",i->name);
				current_iface->is->devaction = -1;
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
			wstatus_locked(w,L"Bringing down %s...",i->name);
			current_iface->is->devaction = 1;
			down_interface(octx,i);
		}
	}
}

void hide_panel_locked(struct panel_state *ps){
	if(ps){
		WINDOW *psw;

		psw = panel_window(ps->p);
		hide_panel(ps->p);
		assert(del_panel(ps->p) == OK);
		ps->p = NULL;
		assert(delwin(psw) == OK);
		ps->ysize = -1;
	}
}

int packet_cb_locked(const interface *i,omphalos_packet *op,struct panel_state *ps){
	iface_state *is = op->i->opaque;
	struct timeval tdiff;
	unsigned long udiff;
	reelbox *rb;

	if(!is){
		return 0;
	}
	if((rb = is->rb) == NULL){
		return 0;
	}
	timersub(&op->tv,&is->lastprinted,&tdiff);
	udiff = timerusec(&tdiff);
	if(udiff < 500000){ // At most one update every 1/2s
		return 0;
	}
	is->lastprinted = op->tv;
	if(rb == current_iface && ps->p){
		iface_details(panel_window(ps->p),i,ps->ysize);
	}
	assert(redraw_iface_generic(rb) == OK);
	return 1;
}

void *interface_cb_locked(interface *i,iface_state *ret,struct panel_state *ps){
	reelbox *rb;

	if(ret == NULL){
		if( (ret = create_interface_state(i)) ){
			int newrb,rows,cols;

			getmaxyx(stdscr,rows,cols);
			if( (newrb = bottom_space_p(rows)) ){
				newrb = rows - newrb;
				if((rb = create_reelbox(ret,rows,newrb,cols)) == NULL){
					free_iface_state(ret);
					free(ret);
					return NULL;
				}
				if(last_reelbox){
					// set up the iface list entries
					ret->next = last_reelbox->is->next;
					ret->next->prev = ret;
					ret->prev = last_reelbox->is;
					last_reelbox->is->next = ret;
					// and also the rb list entries
					if( (rb->next = last_reelbox->next) ){
						rb->next->prev = rb;
					}
					rb->prev = last_reelbox;
					last_reelbox->next = rb;
				}else{
					ret->prev = ret->next = ret;
					rb->next = rb->prev = NULL;
					top_reelbox = rb;
					current_iface = rb;
				}
				last_reelbox = rb;
				// Want the subdisplay left above this new iface,
				// should they intersect.
				assert(bottom_panel(rb->panel) == OK);
			}else{ // insert it after the last visible one, no rb
				ret->next = top_reelbox->is;
				top_reelbox->is->prev->next = ret;
				ret->prev = top_reelbox->is->prev;
				top_reelbox->is->prev = ret;
				ret->rb = NULL;
				rb = NULL;
			}
			++count_interface;
			// calls draw_main_window(), updating iface count
			wstatus_locked(stdscr,L"Set up new interface %s",i->name);
		}
	}else{
		rb = ret->rb;
	}
	if(rb){
		if(rb == current_iface && ps->p){
			iface_details(panel_window(ps->p),i,ps->ysize);
		}
		resize_iface(rb);
		redraw_iface_generic(rb);
	}
	if(interface_up_p(i)){
		if(ret->devaction < 0){
			wstatus_locked(stdscr,L"%s","");
			ret->devaction = 0;
		}
	}else if(ret->devaction > 0){
		wstatus_locked(stdscr,L"%s","");
		ret->devaction = 0;
	}
	return ret; // callers are responsible for screen_update()
}

void interface_removed_locked(iface_state *is,struct panel_state **ps){
	int scrrows,scrcols;
	reelbox *rb;
	
	if(!is){
		return;
	}
	rb = is->rb;
	if(!rb){
		//fprintf(stderr,"Removed hidden interface\n");
	}else{
		int delta = getmaxy(rb->subwin) + 1;

		//fprintf(stderr,"Removing iface at %d\n",rb->scrline);
		assert(werase(rb->subwin) == OK);
		//screen_update();
		assert(hide_panel(rb->panel) == OK);
		getmaxyx(stdscr,scrrows,scrcols);
		// we'll need pull other interfaces up or down
		if(rb->next){
			rb->next->prev = rb->prev;
		}else{
			last_reelbox = rb->prev;
		}
		if(rb->prev){
			rb->prev->next = rb->next;
		}else{
			top_reelbox = rb->next;
		}
		if(rb == current_iface){
			// FIXME need do all the stuff we do in _next_/_prev_
			if((current_iface = rb->next) == NULL){
				current_iface = rb->prev;
			}
			pull_interfaces_up(rb,scrrows,scrcols,delta);
			// give the details window to new current_iface
			if(ps){
				if(current_iface){
					iface_details(panel_window((*ps)->p),get_current_iface(),(*ps)->ysize);
				}else{
					hide_panel_locked(*ps);
					*ps = NULL;
				}
			}
		}else if(rb->scrline > current_iface->scrline){
			pull_interfaces_up(rb,scrrows,scrcols,delta);
			screen_update();
		}else{ // pull them down; removed is above current_iface
			int ts;

			pull_interfaces_down(rb,scrrows,scrcols,delta);
			if( (ts = top_space_p(scrrows)) ){
				pull_interfaces_up(NULL,scrrows,scrcols,ts);
			}
			screen_update();
		}
		free_reelbox(rb);
	}
	free_iface_state(is); // clears l2/l3 nodes
	is->next->prev = is->prev;
	is->prev->next = is->next;
	free(is);
	--count_interface;
	draw_main_window(stdscr); // Update the device count
}

struct l2obj *neighbor_callback_locked(const interface *i,struct l2host *l2){
	struct l2obj *ret;
	iface_state *is;
	reelbox *rb;

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
	if( (rb = is->rb) ){
		resize_iface(rb);
		redraw_iface_generic(rb);
	}
	return ret;
}

struct l4obj *service_callback_locked(const struct interface *i,struct l2host *l2,
					struct l3host *l3,struct l4srv *l4){
	struct l2obj *l2o;
	struct l3obj *l3o;
	struct l4obj *ret;
	iface_state *is;
	reelbox *rb;

	if(((is = i->opaque) == NULL) || !l2){
		return NULL;
	}
	if((l2o = l2host_get_opaque(l2)) == NULL){
		return NULL;
	}
	if((l3o = l3host_get_opaque(l3)) == NULL){
		return NULL;
	}
	if((ret = l4host_get_opaque(l4)) == NULL){
		if((ret = add_service_to_iface(is,l3o,l4)) == NULL){
			return NULL;
		}
	}
	if( (rb = is->rb) ){
		resize_iface(rb);
		redraw_iface_generic(rb);
	}
	return ret;
}

struct l3obj *host_callback_locked(const interface *i,struct l2host *l2,
					struct l3host *l3){
	struct l2obj *l2o;
	struct l3obj *ret;
	iface_state *is;
	reelbox *rb;

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
	if( (rb = is->rb) ){
		resize_iface(rb);
		redraw_iface_generic(rb);
	}
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
	if(bevel(w) != OK){
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
	mvwaddwstr(w,rows - 1,START_COL * 2,statusmsg); // FIXME
	//assert(mvwaddwstr(w,rows - 1,START_COL * 2,statusmsg) != ERR);
	assert(wattroff(w,A_BOLD | COLOR_PAIR(FOOTER_COLOR)) != ERR);
	return OK;

err:
	return ERR;
}

static void
reset_interface_stats(WINDOW *w,const interface *i __attribute__ ((unused))){
	unimplemented(w);
}

void reset_all_interface_stats(WINDOW *w){
	reelbox *rb;

	if( (rb = current_iface) ){
		do{
			const interface *i = rb->is->iface;

			reset_interface_stats(w,i);
		}while((rb = rb->next) != current_iface);
	}
}

void reset_current_interface_stats(WINDOW *w){
	const interface *i;

	if( (i = get_current_iface()) ){
		reset_interface_stats(w,i);
	}
}

// Selecting the previous or next interface (this doesn't apply to an arbitrary
// repositioning): There are two phases to be considered.
//
//  1. There's not enough data to fill the screen. In this case, none will lose
//     or gain visibility, but they might be rotated.
//  2. There's a screen's worth, but not much more than that. An interface might
//     be split across the top/bottom boundaries. Interfaces can be caused to
//     lose or gain visibility.
void use_next_iface_locked(WINDOW *w,struct panel_state *ps){
	int rows,cols,delta;
	reelbox *oldrb;
	reelbox *rb;

	if(!current_iface || current_iface->is->next == current_iface->is){
		return;
	}
	// fprintf(stderr,"Want next interface (%s->%s)\n",current_iface->is->iface->name,
	//		current_iface->is->next->iface->name);
	getmaxyx(w,rows,cols);
	oldrb = current_iface;
	// Don't redraw the old inteface yet; it might have been moved/hidden
	if(current_iface->next == NULL){
		iface_state *is = current_iface->is->next;

		if(is->rb == NULL){ // it's off-screen
			int delta;

			if((is->rb = create_reelbox(is,rows,(rows - 1) - iface_lines_bounded(is,rows),cols)) == NULL){
				return; // FIXME
			}
			current_iface = is->rb;
			delta = -iface_lines_bounded(is,rows);
			if(getbegy(last_reelbox->subwin) + getmaxy(last_reelbox->subwin) >= rows - 1){
				--delta;
			}
			push_interfaces_above(NULL,rows,cols,delta);
			if((current_iface->prev = last_reelbox) == NULL){
				top_reelbox = current_iface;
			}else{
				last_reelbox->next = current_iface;
			}
			current_iface->next = NULL;
			last_reelbox = current_iface;
			if( (delta = top_space_p(rows)) ){
				pull_interfaces_up(NULL,rows,cols,delta);
			}
			redraw_iface_generic(is->rb);
			if(ps->p){
				iface_details(panel_window(ps->p),is->iface,ps->ysize);
			}
			return;
		}
		current_iface = is->rb; // it's at the top
	}else{
		current_iface = current_iface->next; // it's below us
	}
	rb = current_iface;
	// If the newly-selected interface is wholly visible, we'll not need
	// change visibility of any interfaces. If it's above us, we'll need
	// rotate the interfaces 1 unit, moving all. Otherwise, none change
	// position. Redraw all affected interfaces.
	if(iface_wholly_visible_p(rows,rb)){
		if(rb->scrline > oldrb->scrline){ // new is below old
			assert(redraw_iface_generic(oldrb) == OK);
			assert(redraw_iface_generic(rb) == OK);
		}else{ // we were at the bottom (rotate)
			if(top_reelbox->next){
				top_reelbox->next->prev = NULL;
				top_reelbox = top_reelbox->next;
			}else{
				top_reelbox = last_reelbox;
			}
			pull_interfaces_up(rb,rows,cols,getmaxy(rb->subwin) + 1);
			if(last_reelbox){
				rb->scrline = last_reelbox->scrline + getmaxy(last_reelbox->subwin) + 1;
			}else{
				rb->scrline = 1;
			}
			rb->prev = last_reelbox;
			last_reelbox->next = rb;
			rb->next = NULL;
			last_reelbox = rb;
			move_interface_generic(rb,rows,cols,rb->scrline - getbegy(rb->subwin));
		}
	}else{ // new is partially visible...
		if(rb->scrline > oldrb->scrline){ // ...at the bottom
			iface_state *is = current_iface->is;
			int delta = getmaxy(rb->subwin) - iface_lines_bounded(is,rows);

			rb->scrline = rows - (iface_lines_bounded(is,rows) + 1);
			push_interfaces_above(rb,rows,cols,delta);
			move_interface_generic(rb,rows,cols,getbegy(rb->subwin) - rb->scrline);
			assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
			assert(replace_panel(rb->panel,rb->subwin) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}else{ // ...at the top (rotate)
			int delta;

			assert(top_reelbox == rb);
			rb->scrline = rows - 1 - iface_lines_bounded(rb->is,rows);
			top_reelbox->next->prev = NULL;
			top_reelbox = top_reelbox->next;
			delta = -iface_lines_bounded(rb->is,rows);
			if(getbegy(last_reelbox->subwin) + getmaxy(last_reelbox->subwin) >= (rows - 1)){
				--delta;
			}
			push_interfaces_above(NULL,rows,cols,delta);
			rb->next = NULL;
			if( (rb->prev = last_reelbox) ){
				last_reelbox->next = rb;
			}else{
				top_reelbox = rb;
			}
			last_reelbox = rb;
			move_interface_generic(rb,rows,cols,rb->scrline);
			assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
			assert(replace_panel(rb->panel,rb->subwin) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}
	}
	if( (delta = top_space_p(rows)) ){
		pull_interfaces_up(NULL,rows,cols,delta);
	}
	if(ps->p){
		iface_details(panel_window(ps->p),rb->is->iface,ps->ysize);
	}
}

void use_prev_iface_locked(WINDOW *w,struct panel_state *ps){
	reelbox *oldrb;
	int rows,cols;
	reelbox *rb;

	if(!current_iface || current_iface->is->next == current_iface->is){
		return;
	}
	//fprintf(stderr,"Want previous interface (%s->%s)\n",current_iface->is->iface->name,
	//		current_iface->is->prev->iface->name);
	getmaxyx(w,rows,cols);
	oldrb = current_iface;
	// Don't redraw the old interface yet; it might have been moved/hidden
	if(current_iface->prev){
		current_iface = current_iface->prev;
	}else{
		iface_state *is = current_iface->is->prev;

		if(is->rb){
			current_iface = is->rb;
		}else{
			if((is->rb = create_reelbox(is,rows,1,cols)) == NULL){
				return; // FIXME
			}
			current_iface = is->rb;
			push_interfaces_below(NULL,rows,cols,iface_lines_bounded(is,rows) + 1);
			if((current_iface->next = top_reelbox) == NULL){
				last_reelbox = current_iface;
			}else{
				top_reelbox->prev = current_iface;
			}
			current_iface->prev = NULL;
			top_reelbox = current_iface;
			redraw_iface_generic(current_iface);
			if(ps->p){
				iface_details(panel_window(ps->p),is->iface,ps->ysize);
			}
			return;
		}
	}
	rb = current_iface;
	// If the newly-selected interface is wholly visible, we'll not need
	// change visibility of any interfaces. If it's below us, we'll need
	// rotate the interfaces 1 unit, moving all. Otherwise, none need change
	// position. Redraw all affected interfaces.
	if(iface_wholly_visible_p(rows,rb)){
		if(rb->scrline < oldrb->scrline){ // new is above old
			assert(redraw_iface_generic(oldrb) == OK);
			assert(redraw_iface_generic(rb) == OK);
		}else{ // we were at the top
			// Selecting the previous interface is simpler -- we
			// take one from the bottom, and stick it in row 1.
			if(last_reelbox->prev){
				last_reelbox->prev->next = NULL;
				last_reelbox = last_reelbox->prev;
			}else{
				last_reelbox = top_reelbox;
			}
			pull_interfaces_down(rb,rows,cols,getmaxy(rb->subwin) + 1);
			rb->scrline = 1;
			rb->next = top_reelbox;
			top_reelbox->prev = rb;
			rb->prev = NULL;
			top_reelbox = rb;
			move_interface_generic(rb,rows,cols,getbegy(rb->subwin) - rb->scrline);
		}
	}else{ // partially visible...
		iface_state *is = current_iface->is;

		if(rb->scrline < oldrb->scrline){ // ... at the top
			rb->scrline = 1;
			push_interfaces_below(rb,rows,cols,-(getmaxy(rb->subwin) - iface_lines_bounded(is,rows)));
			assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
			assert(replace_panel(rb->panel,rb->subwin) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}else{ // at the bottom
			if(last_reelbox->prev){
				last_reelbox->prev->next = NULL;
				last_reelbox = last_reelbox->prev;
			}else{
				last_reelbox = top_reelbox;
			}
			push_interfaces_below(NULL,rows,cols,iface_lines_bounded(is,rows) + 1);
			rb->scrline = 1;
			if( (rb->next = top_reelbox) ){
				top_reelbox->prev = rb;
			}else{
				last_reelbox = rb;
			}
			rb->prev = NULL;
			top_reelbox = rb;
			move_interface_generic(rb,rows,cols,getbegy(rb->subwin) - rb->scrline);
			assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
			assert(replace_panel(rb->panel,rb->subwin) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}
	}
	if(ps->p){
		iface_details(panel_window(ps->p),rb->is->iface,ps->ysize);
	}
}

int expand_iface_locked(void){
	if(!current_iface){
		return 0;
	}
	expand_interface(current_iface->is);
	assert(resize_iface(current_iface) == OK);
	redraw_iface_generic(current_iface);
	return 0;
}

int collapse_iface_locked(void){
	if(!current_iface){
		return 0;
	}
	collapse_interface(current_iface->is);
	assert(resize_iface(current_iface) == OK);
	redraw_iface_generic(current_iface);
	return 0;
}

void check_consistency(void){
	const reelbox *rb,*prev = NULL;
	int sawcur = 0,expect = 1;

	//fprintf(stderr,"CHECKING CONSISTENCY\n");
	if(top_reelbox){
		assert(!top_reelbox->is->prev->rb || top_reelbox->is->prev->rb == last_reelbox);
	}
	for(rb = top_reelbox ; rb ; rb = rb->next){
		assert(rb->is);
		assert(rb->is->rb == rb);
		assert(!sawcur || rb != current_iface);
		/*fprintf(stderr,"\t%s: %d->%d (%d)\n",rb->is->iface->name,
				getbegy(rb->subwin),getmaxy(rb->subwin) + getbegy(rb->subwin),
				iface_lines_unbounded(rb->is));*/
		if(rb == current_iface){
			sawcur = 1;
		}
		assert(rb->subwin);
		assert(getbegy(rb->subwin) == rb->scrline);
		if(getbegy(rb->subwin) != expect){
			if(expect == 1){
				expect = 2;
			}
		}
		/*if(getbegy(rb->subwin) != expect){
			fprintf(stderr,"\n\n\n\n UH-OH had %d/%d wanted %d\n",
					getbegy(rb->subwin),rb->scrline,expect);
		}*/
		assert(getbegy(rb->subwin) == expect);
		expect += getmaxy(rb->subwin) + 1;
		assert(!panel_hidden(rb->panel));
		assert(prev == rb->prev);
		prev = rb;
	}
	assert(prev == last_reelbox);
	assert((top_reelbox && last_reelbox && current_iface) ||
			(!top_reelbox && !last_reelbox && !current_iface));
	//fprintf(stderr,"CONSISTENT\n");
}

int select_iface_locked(void){
	return -1;
}

int deselect_iface_locked(void){
	return -1;
}

#define ENVROWS 1 // FIXME

static int
env_details(WINDOW *hw,int rows){
	const int col = START_COL;
	const int row = 1;
	int z;

	if((z = rows) >= ENVROWS){
		z = ENVROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (ENVROWS - 1):{
		assert(mvwprintw(hw,row + z,col,"colors: "U64FMT"palette: %s",
				COLORS,modified_colors ? "dynamic" : "fixed") != ERR);
		--z;
		break;
	}default:{
		return ERR;
	}
	}
	return OK;
}

int display_env_locked(WINDOW *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,ENVROWS,0,L"press 'e' to dismiss display")){
		goto err;
	}
	if(env_details(panel_window(ps->p),ps->ysize)){
		goto err;
	}
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
		if(iface_details(panel_window(ps->p),current_iface->is->iface,ps->ysize)){
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
	L"'e': view environment details 'h': toggle this help display",
	L"'q': quit                     ctrl+'L': redraw the screen",
	NULL
};

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

// FIXME need to support scrolling through the list
static int
helpstrs(WINDOW *hw,int row,int rows){
	const wchar_t *hs;
	int z;

	for(z = 0 ; (hs = helps[z]) && z < rows ; ++z){
		assert(mvwaddwstr(hw,row + z,1,hs) != ERR);
	}
	return OK;
}

int display_help_locked(WINDOW *mainw,struct panel_state *ps){
	static const int helprows = sizeof(helps) / sizeof(*helps) - 1; // NULL != row
	const int helpcols = max_helpstr_len(helps) + 4; // spacing + borders

	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,helprows,helpcols,L"press 'h' to dismiss help")){
		goto err;
	}
	if(helpstrs(panel_window(ps->p),1,ps->ysize)){
		goto err;
	}
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
