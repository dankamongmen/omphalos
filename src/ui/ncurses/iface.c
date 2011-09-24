#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <sys/socket.h>
#include <linux/version.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <omphalos/hwaddrs.h>
#include <ncursesw/ncurses.h>
#include <ui/ncurses/iface.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>

typedef struct l3obj {
	struct l3obj *next;
	struct l3host *l3;
} l3obj;

typedef struct l2obj {
	struct l2obj *next;
	struct l2host *l2;
	int cat;			// cached result of l2categorize()
	struct l3obj *l3objs;
} l2obj;

enum {
	EXPANSION_NONE,
	EXPANSION_NODES,
	EXPANSION_HOSTS
};

#define EXPANSION_MAX EXPANSION_HOSTS

iface_state *create_interface_state(interface *i){
	iface_state *ret;
	const char *tstr;

	if((tstr = lookup_arptype(i->arptype,NULL)) == NULL){
		return NULL;
	}
	if( (ret = malloc(sizeof(*ret))) ){
		ret->hosts = ret->nodes = 0;
		ret->l2objs = NULL;
		ret->devaction = 0;
		ret->typestr = tstr;
		ret->lastprinted.tv_sec = ret->lastprinted.tv_usec = 0;
		ret->iface = i;
		ret->expansion = EXPANSION_MAX;
	}
	return ret;
}

static l2obj *
get_l2obj(const interface *i,struct l2host *l2){
	l2obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->cat = l2categorize(i,l2);
		l->l3objs = NULL;
		l->l2 = l2;
	}
	return l;
}

static inline void
free_l3obj(l3obj *l){
	free(l);
}

static inline void
free_l2obj(l2obj *l2){
	l3obj *l3 = l2->l3objs;

	while(l3){
		l3obj *tmp = l3->next;
		free_l3obj(l3);
		l3 = tmp;
	}
	free(l2);
}

static l3obj *
get_l3obj(struct l3host *l3){
	l3obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->l3 = l3;
	}
	return l;
}

// returns < 0 if c0 < c1, 0 if c0 == c1, > 0 if c0 > c1
static inline int
l2catcmp(int c0,int c1){
	// not a surjection! some values are shared.
	static const int vals[__RTN_MAX] = {
		0,			// RTN_UNSPEC
		__RTN_MAX - 1,		// RTN_UNICAST
		__RTN_MAX,		// RTN_LOCAL
		__RTN_MAX - 5,		// RTN_BROADCAST
		__RTN_MAX - 4,		// RTN_ANYCAST
		__RTN_MAX - 3,		// RTN_MULTICAST
		__RTN_MAX - 2,		// RTN_BLACKHOLE
		__RTN_MAX - 2,		// RTN_UNREACHABLE
		__RTN_MAX - 2,		// RTN_PROHIBIT
					// 0 the rest of the way...
	};
	return vals[c0] - vals[c1];
}

l2obj *add_l2_to_iface(const interface *i,iface_state *is,struct l2host *l2h){
	l2obj *l2;

	if( (l2 = get_l2obj(i,l2h)) ){
		l2obj **prev;

		++is->nodes;
		for(prev = &is->l2objs ; *prev ; prev = &(*prev)->next){
			// we want the inverse of l2catcmp()'s priorities
			if(l2catcmp(l2->cat,(*prev)->cat) > 0){
				break;
			}else if(l2catcmp(l2->cat,(*prev)->cat) == 0){
				if(l2hostcmp(l2->l2,(*prev)->l2,i->addrlen) <= 0){
					break;
				}
			}
		}
		l2->next = *prev;
		*prev = l2;
	}
	return l2;
}

l3obj *add_l3_to_iface(iface_state *is,l2obj *l2,struct l3host *l3h){
	l3obj *l3;

	if( (l3 = get_l3obj(l3h)) ){
		l3->next = l2->l3objs;
		l2->l3objs = l3;
		++is->hosts;
	}
	return l3;
}

static void
print_iface_hosts(const interface *i,const iface_state *is,WINDOW *w,
			int rows,int cols,int partial){
	const l2obj *l;
	int line;

	if(is->expansion < EXPANSION_NODES){
		return;
	}
	// If the interface is down, we don't lead with the summary line
	if(partial <= -1){ // didn't print summary line due to space
		line = partial + !!interface_up_p(i);
	}else{
		line = 1 + !!interface_up_p(i);
	}
	for(l = is->l2objs ; l ; l = l->next){
		char hw[HWADDRSTRLEN(i->addrlen)];
		const char *devname;
		char legend;
		int attrs;
		l3obj *l3;
		
		if(line >= rows - (partial <= 0)){
			break;
		}
		if(line >= 0){
			switch(l->cat){
				case RTN_UNICAST:
					attrs = COLOR_PAIR(UCAST_COLOR);
					devname = get_devname(l->l2);
					legend = 'U';
					break;
				case RTN_LOCAL:
					attrs = A_BOLD | COLOR_PAIR(LCAST_COLOR);
					if(interface_virtual_p(i) ||
						(devname = get_devname(l->l2)) == NULL){
						devname = i->topinfo.devname;
					}
					legend = 'L';
					break;
				case RTN_MULTICAST:
					attrs = A_BOLD | COLOR_PAIR(MCAST_COLOR);
					devname = get_devname(l->l2);
					legend = 'M';
					break;
				case RTN_BROADCAST:
					attrs = COLOR_PAIR(BCAST_COLOR);
					devname = get_devname(l->l2);
					legend = 'B';
					break;
				default:
					assert(0 && "Unknown l2 category");
					break;
			}
			if(!interface_up_p(i)){
				attrs = (attrs & A_BOLD) | COLOR_PAIR(DBORDER_COLOR);
			}
			assert(wattrset(w,attrs) != ERR);
			l2ntop(l->l2,i->addrlen,hw);
			if(devname){
				size_t len = strlen(devname);

				if(len > cols - 5 - HWADDRSTRLEN(i->addrlen)){
					len = cols - 5 - HWADDRSTRLEN(i->addrlen);
				}
				assert(mvwprintw(w,line,2,"%c %s %.*s",
					legend,hw,cols - 1,devname) != ERR);
			}else{
				assert(mvwprintw(w,line,2,"%c %s",legend,hw) != ERR);
			}
			if(interface_up_p(i)){
				char sbuf[PREFIXSTRLEN + 1],dbuf[PREFIXSTRLEN + 1];
				if(get_srcpkts(l->l2) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
					mvwprintw(w,line,cols - PREFIXSTRLEN * 1 - 1,PREFIXFMT,
							prefix(get_dstpkts(l->l2),1,dbuf,sizeof(dbuf),1));
				}else{
					mvwprintw(w,line,cols - PREFIXSTRLEN * 2 - 2,PREFIXFMT" "PREFIXFMT,prefix(get_srcpkts(l->l2),1,sbuf,sizeof(sbuf),1),
							prefix(get_dstpkts(l->l2),1,dbuf,sizeof(dbuf),1));
				}
			}
		}
		++line;
		if(is->expansion >= EXPANSION_HOSTS){
			for(l3 = l->l3objs ; l3 ; l3 = l3->next){
				char nw[INET6_ADDRSTRLEN + 1]; // FIXME
				const char *name;

				if(line >= rows - (partial <= 0)){
					break;
				}
				if(line >= 0){
					l3ntop(l3->l3,nw,sizeof(nw));
					if((name = get_l3name(l3->l3)) == NULL){
						name = "";
					}
					assert(mvwprintw(w,line,5,"%s %s",nw,name) != ERR);
					{
						char sbuf[PREFIXSTRLEN + 1];
						char dbuf[PREFIXSTRLEN + 1];
						if(l3_get_srcpkt(l3->l3) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
							mvwprintw(w,line,cols - PREFIXSTRLEN * 1 - 1,PREFIXFMT,
									prefix(l3_get_dstpkt(l3->l3),1,dbuf,sizeof(dbuf),1));
						}else{
							mvwprintw(w,line,cols - PREFIXSTRLEN * 2 - 2,PREFIXFMT" "PREFIXFMT,
									prefix(l3_get_srcpkt(l3->l3),1,sbuf,sizeof(sbuf),1),
									prefix(l3_get_dstpkt(l3->l3),1,dbuf,sizeof(dbuf),1));
						}
					}
					assert(wattrset(w,attrs) != ERR);
				}
				++line;
			}
		}
	}
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

static void
iface_box(const interface *i,const iface_state *is,WINDOW *w,int active,
						int partial){
	int bcolor,hcolor,rows,cols;
	size_t buslen;
	int attrs;

	getmaxyx(w,rows,cols);
	bcolor = interface_up_p(i) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = interface_up_p(i) ? UHEADING_COLOR : DHEADING_COLOR;
	attrs = active ? A_REVERSE : A_BOLD;
	assert(wattrset(w,attrs | COLOR_PAIR(bcolor)) == OK);
	if(partial == 0){
		assert(bevel(w) == OK);
	}else if(partial < 0){
		assert(0);
		assert(bevel_notop(w) == OK);
	}else{
		assert(bevel_nobottom(w) == OK);
	}
	assert(wattroff(w,A_REVERSE) == OK);

	if(partial >= 0){
		if(active){
			assert(wattron(w,A_BOLD) == OK);
		}
		assert(mvwprintw(w,0,1,"[") != ERR);
		assert(wcolor_set(w,hcolor,NULL) == OK);
		if(active){
			assert(wattron(w,A_BOLD) == OK);
		}else{
			assert(wattroff(w,A_BOLD) == OK);
		}
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
		if(active){
			assert(wattron(w,A_BOLD) == OK);
		}
		assert(wprintw(w,"]") != ERR);
		assert(wmove(w,0,cols - 4) != ERR);
		assert(wattron(w,A_BOLD) == OK);
		assert(waddwstr(w,is->expansion == EXPANSION_MAX ? L"[-]" :
					is->expansion == 0 ? L"[+]" : L"[±]") != ERR);
		assert(wattron(w,attrs) != ERR);
		assert(wattroff(w,A_REVERSE) != ERR);
	}
	if(partial <= 0){
		assert(mvwprintw(w,rows - 1,2,"[") != ERR);
		assert(wcolor_set(w,hcolor,NULL) != ERR);
		if(active){
			assert(wattron(w,A_BOLD) == OK);
		}else{
			assert(wattroff(w,A_BOLD) == OK);
		}
		assert(wprintw(w,"mtu %d",i->mtu) != ERR);
		if(interface_up_p(i)){
			char buf[U64STRLEN + 1];

			assert(iface_optstr(w,"up",hcolor,bcolor) != ERR);
			if(i->settings_valid == SETTINGS_VALID_ETHTOOL){
				if(!interface_carrier_p(i)){
					assert(waddstr(w," (no carrier)") != ERR);
				}else{
					assert(wprintw(w," (%sb %s)",prefix(i->settings.ethtool.speed * 1000000u,1,buf,sizeof(buf),1),
								duplexstr(i->settings.ethtool.duplex)) != ERR);
				}
			}else if(i->settings_valid == SETTINGS_VALID_WEXT){
				if(i->settings.wext.mode == NL80211_IFTYPE_MONITOR){
					assert(wprintw(w," (%s)",modestr(i->settings.wext.mode)) != ERR);
				}else if(!interface_carrier_p(i)){
					assert(wprintw(w," (%s, no carrier)",modestr(i->settings.wext.mode)) != ERR);
				}else{
					assert(wprintw(w," (%sb %s ",prefix(i->settings.wext.bitrate,1,buf,sizeof(buf),1),
								modestr(i->settings.wext.mode)) != ERR);
					if(i->settings.wext.freq <= MAX_WIRELESS_CHANNEL){
						assert(wprintw(w,"ch %ju)",i->settings.wext.freq) != ERR);
					}else{
						assert(wprintw(w,"%sHz)",prefix(i->settings.wext.freq,1,buf,sizeof(buf),1)) != ERR);
					}
				}
			}
		}else{
			assert(iface_optstr(w,"down",hcolor,bcolor) != ERR);
			if(i->settings_valid == SETTINGS_VALID_WEXT){
				assert(wprintw(w," (%s)",modestr(i->settings.wext.mode)) != ERR);
			}
		}
		if(interface_promisc_p(i)){
			assert(iface_optstr(w,"promisc",hcolor,bcolor) != ERR);
		}
		assert(wcolor_set(w,bcolor,NULL) != ERR);
		if(active){
			assert(wattron(w,A_BOLD) == OK);
		}
		assert(wprintw(w,"]") != ERR);
		if( (buslen = strlen(i->drv.bus_info)) ){
			if(active){
				// FIXME Want the text to be bold -- currently unreadable
				assert(wattrset(w,A_REVERSE | COLOR_PAIR(bcolor)) != ERR);
			}else{
				assert(wattrset(w,COLOR_PAIR(bcolor) | A_BOLD) != ERR);
			}
			if(i->busname){
				buslen += strlen(i->busname) + 1;
				assert(mvwprintw(w,rows - 1,cols - (buslen + 2),
						"%s:%s",i->busname,i->drv.bus_info) != ERR);
			}else{
				assert(mvwprintw(w,rows - 1,cols - (buslen + 2),
						"%s",i->drv.bus_info) != ERR);
			}
		}
	}
}

static void
print_iface_state(const interface *i,const iface_state *is,WINDOW *w,
				int rows,int cols,int partial){
	char buf[U64STRLEN + 1],buf2[U64STRLEN + 1];
	unsigned long usecdomain;

	if(partial < -1 || rows < 2){
		return;
	}
	assert(wattrset(w,A_BOLD | COLOR_PAIR(IFACE_COLOR)) != ERR);
	// FIXME broken if bps domain ever != fps domain. need unite those
	// into one FTD stat by letting it take an object...
	// FIXME this leads to a "ramp-up" period where we approach steady state
	usecdomain = i->bps.usec * i->bps.total;
	assert(mvwprintw(w,1,1,"%u node%s. Last %lus: %7sb/s (%sp)",
		is->nodes,is->nodes == 1 ? "" : "s",
		usecdomain / 1000000,
		prefix(timestat_val(&i->bps) * CHAR_BIT * 1000000 * 100 / usecdomain,100,buf,sizeof(buf),0),
		prefix(timestat_val(&i->fps),1,buf2,sizeof(buf2),1)) != ERR);
	assert(mvwprintw(w,1,cols - PREFIXSTRLEN * 2 - 5,"Total: Src     Dst") != ERR);
}

void free_iface_state(iface_state *is){
	l2obj *l2 = is->l2objs;

	while(l2){
		l2obj *tmp = l2->next;
		free_l2obj(l2);
		l2 = tmp;
	}
}

int redraw_iface(const iface_state *is,const reelbox *rb,int active){
	int rows,cols,partial,scrrows,scrcols;
	const interface *i = is->iface;

	if(panel_hidden(rb->panel)){
		return OK;
	}
	getmaxyx(stdscr,scrrows,scrcols);
	if(rb->scrline >= scrrows){ // no top
		partial = -1;
	}else if(iface_wholly_visible_p(scrrows,rb) || active){ // completely visible
		partial = 0;
	}else{
		partial = 1; // no bottom
	}
	getmaxyx(rb->subwin,rows,cols);
	assert(cols < scrcols); // FIXME
	assert(werase(rb->subwin) != ERR);
	if(partial >= 0){
		iface_box(i,is,rb->subwin,active,partial);
		if(interface_up_p(i)){
			print_iface_state(i,is,rb->subwin,rows,cols,partial);
		}
		print_iface_hosts(i,is,rb->subwin,0/*rows*/,cols,partial);
	}
	return OK;
}

// Will even a line of the interface be visible as stands, post-move?
int iface_visible_p(int rows,const reelbox *rb){
	if(rb->scrline < 1){
		if(rb->next && rb->next->scrline > 2){
			return 1;
		}
		return 0;
	}
	if(rb->scrline < rows - 1){
		return 1; // at least partially visible at the bottom
	}else if(rb->next && rb->next->scrline < rb->scrline){
		if(rb->next->scrline < rows - 1 && rb->next->scrline > 2){
			return 1; // we're partially visible at the top
		}
	}
	return 0;
}

// Move this interface, possibly hiding it or bringing it onscreen. Negative
// delta indicates movement up, positive delta moves down. Returns a non-zero
// if the interface is active and would be pushed offscreen.
void move_interface(iface_state *is,reelbox *rb,int rows,int cols,
					int delta,int active){
	int partiallyvis,whollyvis;
       
	partiallyvis = iface_visible_p(rows,rb);
	whollyvis = iface_wholly_visible_p(rows,rb);
	if(rb->scrline < 0){
		rb->scrline = rows; // invalidate it
	}
	if(iface_wholly_visible_p(rows,rb)){
		assert(move_panel(rb->panel,rb->scrline,1) != ERR);
		if(!whollyvis){
			assert(wresize(rb->subwin,iface_lines_bounded(is,rows),PAD_COLS(cols)) == OK);
			if(!partiallyvis){
				assert(show_panel(rb->panel) == OK);
			}
		}
		assert(redraw_iface(is,rb,active) == OK);
	}else if(iface_visible_p(rows,rb)){
		int nlines,rr,targ;

		rr = getmaxy(rb->subwin);
		if(delta > 0){
			targ = rb->scrline;
			nlines = rows - rb->scrline - 1; // sans-bottom partial
		}else{
			targ = 1;
			nlines = rb->next->scrline - 1;
		}
		// FIXME this shouldn't be necessary. replace with assert(nlines >= 1);
		if(nlines < 1){
			assert(werase(rb->subwin) != ERR);
			assert(hide_panel(rb->panel) != ERR);
			return;
		}else if(nlines > rr){
			assert(move_panel(rb->panel,targ,1) == OK);
			assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) == OK);
		}else if(nlines < rr){
			assert(wresize(rb->subwin,nlines,PAD_COLS(cols)) == OK);
			assert(move_panel(rb->panel,targ,1) == OK);
		}else{
			assert(move_panel(rb->panel,targ,1) == OK);
		}
		if(!partiallyvis){
			assert(show_panel(rb->panel) == OK);
		}
		assert(redraw_iface(is,rb,active) == OK);
	}else if(!panel_hidden(rb->panel)){
		assert(werase(rb->subwin) != ERR);
		assert(hide_panel(rb->panel) != ERR);
	}
	return;
}

// This is the number of lines we'd have in an optimal world; we might have
// fewer available to us on this screen at this time.
int lines_for_interface(const iface_state *is){
	return 2 + interface_up_p(is->iface) +
		((is->expansion < EXPANSION_NODES) ? 0 : is->nodes) +
		((is->expansion < EXPANSION_HOSTS) ? 0 : is->hosts);
}

// Is the interface window entirely visible? We can't draw it otherwise, as it
// will obliterate the global bounding box.
int iface_wholly_visible_p(int rows,const reelbox *rb){
	const iface_state *is = rb->is;

	if(rows < 0){
		int cols;

		getmaxyx(stdscr,rows,cols);
		assert(cols >= 0);
	}
	if(rb->scrline + iface_lines_bounded(is,rows) >= rows){
		return 0;
	}else if(rb->scrline < 1){
		return 0;
	}
	return 1;
}

void expand_interface(iface_state *is){
	if(is->expansion == EXPANSION_MAX){
		return;
	}
	++is->expansion;
}

void collapse_interface(iface_state *is){
	if(is->expansion == 0){
		return;
	}
	--is->expansion;
}
