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
// dequeue + single selection
static reelbox *current_iface,*top_reelbox,*last_reelbox;

// Status bar at the bottom of the screen. Must be reallocated upon screen
// resize and allocated based on initial screen at startup. Don't shrink
// it; widening the window again should show the full message.
static char *statusmsg;
static int statuschars;	// True size, not necessarily what's available

static int resize_iface(reelbox *);

// Caller needs set up: next, prev
static reelbox *
create_reelbox(iface_state *is,int scrline,int cols){
	reelbox *ret;

	if( (ret = malloc(sizeof(*ret))) ){
		if((ret->subwin = newwin(lines_for_interface(is),PAD_COLS(cols),scrline,START_COL)) == NULL ||
				(ret->panel = new_panel(ret->subwin)) == NULL){
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
iface_lines_unbounded(const struct iface_state *is){
	return iface_lines_bounded(is,INT_MAX);
}

static inline int
redraw_iface_generic(const reelbox *rb){
	return redraw_iface(rb->is,rb,rb == current_iface);
}

static inline void
move_interface_generic(reelbox *rb,int rows,int cols,int delta){
	move_interface(rb->is,rb,rows,cols,delta,rb == current_iface);
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
	assert(bevel(psw) == OK);
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

static void
free_reelbox(reelbox *rb){
	if(rb){
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
// ->is->iface is no longer valid).
static void
pull_interfaces_down(reelbox *puller,int rows,int cols,int delta){
	reelbox *rb;

	for(rb = puller->prev ; rb ; rb = rb->prev){
		rb->scrline += delta;
		move_interface_generic(rb,rows,cols,delta);
	}
	if(top_reelbox){
		if(top_reelbox->scrline <= 1){
			return;
		}
	}
	// FIXME make more visible
}

static void
pull_interfaces_up(reelbox *puller,int rows,int cols,int delta){
	reelbox *rb;

	for(rb = puller->next ; rb ; rb = rb->next){
		rb->scrline -= delta;
		move_interface_generic(rb,rows,cols,-delta);
	}
	if(last_reelbox){
		if(last_reelbox->scrline + getmaxy(last_reelbox->subwin) >= rows - 1){
			return;
		}
	}
	// FIXME make more visible
}

// An interface (pusher) has had its bottom border moved up or down (positive or
// negative delta, respectively). Update the interfaces below it on the screen
// (all those up until those actually displayed above it on the screen). Should
// be called before pusher->scrline has been updated.
static void
push_interfaces_below(reelbox *pusher,int rows,int cols,int delta){
	reelbox *rb;

	for(rb = last_reelbox ; rb ; rb = rb->prev){
		if(rb == pusher){
			break;
		}
		rb->scrline += delta;
		move_interface_generic(rb,rows,cols,delta);
	}
	while(panel_hidden(last_reelbox->panel)){
		reelbox *t;

		t = last_reelbox->prev;
		free_reelbox(last_reelbox);
		if((last_reelbox = t) == NULL){
			top_reelbox = NULL;
			break;
		}else{
			t->next = NULL;
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

	for(rb = top_reelbox ; rb ; rb = rb->next){
		if(rb == pusher){
			break;
		}
		rb->scrline += delta;
		move_interface_generic(rb,rows,cols,delta);
	}
	while(panel_hidden(top_reelbox->panel)){
		reelbox *t;

		t = top_reelbox->next;
		free_reelbox(top_reelbox);
		if((top_reelbox = t) == NULL){
			last_reelbox = NULL;
			break;
		}else{
			t->prev = NULL;
		}
	}
	// Now, if our delta was negative, see if we pulled any down below us
	// FIXME pull_interfaces_up(pusher,rows,cols,delta);
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
	getmaxyx(rb->subwin,subrows,subcols);
	assert(subcols); // FIXME
	if(nlines < subrows){ // Shrink the interface
		//int delta = subrows - nlines;
		// FIXME if we're above the current interface, we shrink and
		// then move down, pulling things from above
		assert(werase(rb->subwin) == OK);
		screen_update();
		assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) != ERR);
		assert(replace_panel(rb->panel,rb->subwin) != ERR);
		// FIXME pull_interfaces_up(rb,rows,cols,delta);
	}else if(nlines > subrows){ // Expand the interface
		// The current interface never becomes a partial interface. We
		// don't try to make it one here, and move_interface() will
		// refuse to perform a move resulting in one.
		if(i == curi){
			// The current interface can grow in either direction.
			// FIXME take what space is available even if we can't
			// grow all the way!
			int delta = nlines - subrows;

			if(nlines + rb->scrline < rows){ // Try down first.
				push_interfaces_below(rb,rows,cols,delta);
				assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) != ERR);
				assert(replace_panel(rb->panel,rb->subwin) != ERR);
			}else if(rb->scrline > delta){ // Otherwise try up
				if(delta > rb->scrline - 1){
					delta = rb->scrline - 1;
				}
				rb->scrline -= delta;
				push_interfaces_above(rb,rows,cols,-delta);
				// assert(move_interface_generic(is,rows,cols,-delta) == OK);
				assert(move_panel(rb->panel,rb->scrline,1) != ERR);
				assert(wresize(rb->subwin,iface_lines_bounded(is,rows),PAD_COLS(cols)) != ERR);
				assert(replace_panel(rb->panel,rb->subwin) != ERR);
			}
		}else if(rb->scrline > current_iface->scrline){ // go down
			if(nlines + rb->scrline < rows){
				int delta = nlines - subrows;

				push_interfaces_below(rb,rows,cols,delta);
				assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) != ERR);
				assert(replace_panel(rb->panel,rb->subwin) != ERR);
			}else{ // else becomes a partial interface
				// FIXME take any available space!
			}
		}else{
			if(rb->scrline != 1){ // we can only go up
				int delta = nlines - subrows;

				if(delta > rb->scrline - 1){
					delta = rb->scrline - 1;
				}
				rb->scrline -= delta;
				push_interfaces_above(rb,rows,cols,-delta);
				// assert(move_interface_generic(is,rows,cols,-delta) == OK);
				assert(move_panel(rb->panel,rb->scrline,1) != ERR);
				assert(wresize(rb->subwin,iface_lines_bounded(is,rows),PAD_COLS(cols)) != ERR);
				assert(replace_panel(rb->panel,rb->subwin) != ERR);
			}else{ // else becomes a partial interface
				// FIXME take any available space!
			}
		}
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
			wstatus_locked(w,"Bringing down %s...",i->name);
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
		del_panel(ps->p);
		ps->p = NULL;
		delwin(psw);
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

static inline int
room_for_newbox(int rows){
	if(last_reelbox){
		int r = last_reelbox->scrline + getmaxy(last_reelbox->subwin) + 1;

		if(r >= rows - 1){
			return 0;
		}
		return r;
	}
	return 1;
}

void *interface_cb_locked(interface *i,iface_state *ret,struct panel_state *ps){
	reelbox *rb;

	if(ret == NULL){
		if( (ret = create_interface_state(i)) ){
			int newrb,rows,cols;

			getmaxyx(stdscr,rows,cols);
			if( (newrb = room_for_newbox(rows)) ){
				if((rb = create_reelbox(ret,newrb,cols)) == NULL){
					free_iface_state(ret);
					free(ret);
					return NULL;
				}
				wstatus_locked(stdscr,"adding %s",i->name);
				doupdate();
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
				ret->prev = last_reelbox->is;
				last_reelbox->is->next->prev = ret;
				ret->next = last_reelbox->is->next;
				last_reelbox->is->next = ret;
				rb = NULL;
			}
			++count_interface;
			// calls draw_main_window(), updating iface count
			wstatus_locked(stdscr,"Set up new interface %s",i->name);
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
			wstatus_locked(stdscr,"%s","");
			ret->devaction = 0;
		}
	}else if(ret->devaction > 0){
		wstatus_locked(stdscr,"%s","");
		ret->devaction = 0;
	}
	return ret; // callers are responsible for screen_update()
}

void interface_removed_locked(iface_state *is,struct panel_state *ps){
	reelbox *rb;
	
	if(!is){
		return;
	}
	rb = is->rb;
	free(is);
	is->next->prev = is->prev;
	is->prev->next = is->next;
	if(!rb){
		return;
	}
	//const int visible = !panel_hidden(rb->panel);
	//int rows,cols;

	free_iface_state(is); // clears l2/l3 nodes
	--count_interface;
	wclear(rb->subwin);
	del_panel(rb->panel);
	//getmaxyx(rb->subwin,rows,cols);
	delwin(rb->subwin);
	if(rb->next || rb->prev){
		int scrrows,scrcols;
		//reelbox *ci;

		// First, splice it out of the list
		if(rb->next){
			rb->next->prev = rb->prev;
		}
		if(rb->prev){
			rb->prev->next = rb->next;
		}
		if(top_reelbox == rb){
			top_reelbox = rb->next;
		}
		if(last_reelbox == rb){
			last_reelbox = rb->prev;
		}
		getmaxyx(stdscr,scrrows,scrcols);
		// we'll need pull other interfaces up or down
		if(rb == current_iface || rb->scrline > current_iface->scrline){
			// FIXME don't count the +1 when we extend to the bottom
			// of the screen!
			pull_interfaces_up(rb,scrrows,scrcols,getmaxy(rb->subwin) + 1);
		}else{ // pull them down; we're above current_iface
			pull_interfaces_down(rb,scrrows,scrcols,getmaxy(rb->subwin));
			// There might not be sufficient data to pull them down
			// in which case pull down what we can, then pull up,
			// then possibly move everything up. FIXME
		}
		if(rb == current_iface){
			if((current_iface = rb->prev) == NULL){
				current_iface = rb->next;
			}
			// give the details window to new current_iface
			if(ps->p){
				iface_details(panel_window(ps->p),get_current_iface(),ps->ysize);
			}
		}
		/*
		assert(cols);
		if(visible){
			for(ci = rb->next ; ci->scrline > rb->scrline ; ci = ci->next){
				move_interface_generic(ci,scrrows,PAD_COLS(scrcols),-(rows + 1));
			}
		}*/
	}else{
		// If details window exists, destroy it FIXME
		top_reelbox = last_reelbox = current_iface = NULL;
	}
	draw_main_window(stdscr); // Update the device count
	free(rb);
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

struct l3obj *host_callback_locked(const interface *i,struct l2host *l2,struct l3host *l3){
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
	reelbox *oldrb;
	int rows,cols;
	reelbox *rb;

	if(!current_iface){
		return;
	}
	getmaxyx(w,rows,cols);
	oldrb = current_iface;
	// Don't redraw the old inteface yet; it might have been moved/hidden
	if((current_iface = current_iface->next) == NULL){
		current_iface = top_reelbox;
	}
	rb = current_iface;
	assert(rb);
	// If the newly-selected interface is wholly visible, we'll not need
	// change visibility of any interfaces. If it's above us, we'll need
	// rotate the interfaces 1 unit, moving all. Otherwise, none need change
	// position. Redraw all affected interfaces.
	if(iface_wholly_visible_p(rows,rb)){
		if(rb->scrline > oldrb->scrline){ // new is below old
			assert(redraw_iface_generic(oldrb) == OK);
			assert(redraw_iface_generic(rb) == OK);
		}else{
			// We might not have a full screen -- base the new
			// location off the old one's, which must have been the
			// last interface in the display if we're rotating.
			//
			// Both have up-to-date sizes -- those aren't changing.
			push_interfaces_above(oldrb,rows,cols,-(getmaxy(rb->subwin)));
			rb->scrline = oldrb->scrline + getmaxy(oldrb->subwin) - getmaxy(rb->subwin);
			assert(move_panel(rb->panel,rb->scrline,START_COL) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}
	}else{ // We'll need change visibilities
		int up;

		// We need them to move up however many spaces we need
		// to move in. We'll need one per line not currently
		// visible, plus a boundary line if applicable (there
		// isn't one currently, and we don't fill the screen).
		rb->scrline = rows - iface_lines_bounded(rb->is,rows) - 1;
		up = oldrb->scrline + iface_lines_bounded(oldrb->is,rows)
			+ 1 - rb->scrline;
		assert(up > 0);
		push_interfaces_above(rb,rows,cols,-up);
		assert(move_panel(rb->panel,rb->scrline,START_COL) != ERR);
		if(panel_hidden(rb->panel)){
			assert(show_panel(rb->panel) != ERR);
		}
		assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
		assert(replace_panel(rb->panel,rb->subwin) != ERR);
		assert(redraw_iface_generic(oldrb) == OK);
	}
	if(panel_hidden(oldrb->panel)){
		// we hid the entire panel, and thus might have space
		// to move up into. move as many interfaces as we can
		// onscreen FIXME
	}
	if(ps->p){
		assert(top_panel(ps->p) != ERR);
		iface_details(panel_window(ps->p),rb->is->iface,ps->ysize);
	}
}

void use_prev_iface_locked(WINDOW *w,struct panel_state *ps){
	reelbox *oldrb;
	int rows,cols;
	reelbox *rb;

	if(!current_iface){
		return;
	}
	getmaxyx(w,rows,cols);
	oldrb = current_iface;
	// Don't redraw the old inteface yet; it might have been moved/hidden
	// FIXME current_iface->prev could be NULL
	if((current_iface = current_iface->prev) == NULL){
		current_iface = last_reelbox;
	}
	rb = current_iface;
	assert(rb);
	// If the newly-selected interface is wholly visible, we'll not need
	// change visibility of any interfaces. If it's below us, we'll need
	// rotate the interfaces 1 unit, moving all. Otherwise, none need change
	// position. Redraw all affected interfaces.
	if(iface_wholly_visible_p(rows,rb)){
		if(rb->scrline < oldrb->scrline){ // new is above old
			assert(redraw_iface_generic(oldrb) == OK);
			assert(redraw_iface_generic(rb) == OK);
		}else{
			// Selecting the previous interface is simpler -- we
			// take one from the bottom, and stick it in row 1.
			push_interfaces_below(oldrb,rows,cols,getmaxy(rb->subwin));
			rb->scrline = 1;
			assert(move_panel(rb->panel,rb->scrline,START_COL) != ERR);
			assert(redraw_iface_generic(rb) == OK);
		}
	}else{ // We'll need change visibilities
		int down;

		rb->scrline = 1;
		// We need them to move up however many spaces we need
		// to move in. We'll need one per line not currently
		// visible, plus a boundary line if applicable (there
		// isn't one currently, and we don't fill the screen).
		assert(getmaxy(rb->subwin) > 0);
		down = iface_lines_bounded(rb->is,rows) - getmaxy(rb->subwin);
		assert(down > 0);
		push_interfaces_above(rb,rows,cols,-down);
		assert(move_panel(rb->panel,rb->scrline,START_COL) != ERR);
		if(panel_hidden(rb->panel)){
			assert(show_panel(rb->panel) != ERR);
		}
		assert(wresize(rb->subwin,iface_lines_bounded(rb->is,rows),PAD_COLS(cols)) == OK);
		assert(replace_panel(rb->panel,rb->subwin) != ERR);
		assert(redraw_iface_generic(oldrb) == OK);
	}
	if(panel_hidden(oldrb->panel)){
		// we hid the entire panel, and thus might have space
		// to move up into. move as many interfaces as we can
		// onscreen FIXME
	}
	if(ps->p){
		assert(top_panel(ps->p) != ERR);
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

	if(top_reelbox){
		assert(!top_reelbox->is->prev->rb || top_reelbox->is->prev->rb == last_reelbox);
	}
	for(rb = top_reelbox ; rb ; rb = rb->next){
		assert(!sawcur || rb != current_iface);
		if(rb == current_iface){
			sawcur = 1;
		}
		assert(rb->subwin);
		assert(getbegy(rb->subwin) == expect);
		expect += getmaxy(rb->subwin) + 1;
		assert(!panel_hidden(rb->panel));
		assert(prev == rb->prev);
		prev = rb;
	}
	assert(prev == last_reelbox);
	assert((top_reelbox && last_reelbox && current_iface) ||
			(!top_reelbox && !last_reelbox && !current_iface));
}
