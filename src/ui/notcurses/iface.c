#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/version.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <omphalos/service.h>
#include <omphalos/hwaddrs.h>
#include <ui/notcurses/core.h>
#include <ui/notcurses/util.h>
#include <omphalos/netaddrs.h>
#include <omphalos/wireless.h>
#include <ui/notcurses/color.h>
#include <ui/notcurses/iface.h>
#include <omphalos/interface.h>
#include <notcurses/notcurses.h>

typedef struct l4obj {
	struct l4obj *next;
	struct l4srv *l4;
	unsigned emph;			// Emphasize the service in display?
} l4obj;

typedef struct l3obj {
	struct l3obj *next;
	struct l3host *l3;
	struct l4obj *l4objs;
	struct l2obj *l2;		// FIXME coverup of real failure
} l3obj;

typedef struct l2obj {
	struct l2obj *next,*prev;
	struct l2host *l2;
	unsigned lnes;			// number of lines node would take up
	int cat;			// cached result of l2categorize()
	struct l3obj *l3objs;
} l2obj;

static void
draw_right_vline(const interface *i,int active,WINDOW *w){
	//assert(i && w && (active || !active));
	int co = interface_up_p(i) ? UBORDER_COLOR : DBORDER_COLOR;
	cchar_t bchr[] = {
		{
		.attr = (active ? A_REVERSE : A_BOLD) | COLOR_PAIR(co),
		.chars = L"│",
		},
	};

	//wattrset(w,0);
	wins_wch(w,&bchr[0]);
}

l2obj *l2obj_next(l2obj *l2){
	return l2->next;
}

l2obj *l2obj_prev(l2obj *l2){
	return l2->prev;
}

int l2obj_lines(const l2obj *l2){
	return l2->lnes;
}

iface_state *create_interface_state(interface *i){
	iface_state *ret;
	const char *tstr;

	if((tstr = lookup_arptype(i->arptype,NULL,NULL)) == NULL){
		return NULL;
	}
	if( (ret = malloc(sizeof(*ret))) ){
		ret->srvs = ret->hosts = ret->nodes = ret->vnodes = 0;
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
get_l2obj(const interface *i,const iface_state *is,struct l2host *l2){
	l2obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->lnes = is->expansion > EXPANSION_NONE;
		l->cat = l2categorize(i,l2);
		l->l3objs = NULL;
		l->l2 = l2;
	}
	return l;
}

static inline void
free_l4obj(l4obj *l4){
	free(l4);
}

static inline void
free_l3obj(l3obj *l){
	l4obj *l4 = l->l4objs;

	while(l4){
		l4obj *tmp = l4->next;
		free_l4obj(l4);
		l4 = tmp;
	}
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

static l4obj *
get_l4obj(struct l4srv *srv,unsigned emph){
	l4obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->l4 = srv;
		l->emph = emph;
	}
	return l;
}

static l3obj *
get_l3obj(struct l3host *l3){
	l3obj *l;

	if( (l = malloc(sizeof(*l))) ){
		l->l3 = l3;
		l->l4objs = NULL;
	}
	return l;
}

static unsigned
node_lines(int e,const l2obj *l){
	const l3obj *l3;
	unsigned lnes;

	if(e == EXPANSION_NONE){
		return 0;
	}
	lnes = 1;
	if(e > EXPANSION_NODES){
		for(l3 = l->l3objs ; l3 ; l3 = l3->next){
			++lnes;
			if(e > EXPANSION_HOSTS){
				lnes += !!l3->l4objs;
			}
		}
	}
	return lnes;
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

	if( (l2 = get_l2obj(i,is,l2h)) ){
		if(is->nodes == 0 && is->vnodes == 0){
			is->l2objs = l2;
			l2->next = l2->prev = NULL;
		}else{
			l2obj **prev,*prec;

			for(prec = NULL, prev = &is->l2objs ; *prev ; prec = *prev, prev = &(*prev)->next){
				// we want the inverse of l2catcmp()'s priorities
				if(l2catcmp(l2->cat,(*prev)->cat) > 0){
					break;
				}else if(l2catcmp(l2->cat,(*prev)->cat) == 0){
					if(l2hostcmp(l2->l2,(*prev)->l2,i->addrlen) <= 0){
						break;
					}
				}
			}
			if( (l2->next = *prev) ){
				l2->prev = (*prev)->prev;
				(*prev)->prev = l2;
			}else{
				l2->prev = prec;
			}
			*prev = l2;
		}
		if(l2->cat == RTN_LOCAL || l2->cat == RTN_UNICAST){
			++is->nodes;
		}else{
			++is->vnodes;
		}
	}
	return l2;
}

l3obj *add_l3_to_iface(iface_state *is,l2obj *l2,struct l3host *l3h){
	l3obj *l3;

	if( (l3 = get_l3obj(l3h)) ){
		l3->next = l2->l3objs;
		l2->l3objs = l3;
		l3->l2 = l2;
		if(is->expansion >= EXPANSION_HOSTS){
			++l2->lnes;
		}
		++is->hosts;
	}
	return l3;
}

l4obj *add_service_to_iface(iface_state *is,struct l2obj *l2,struct l3obj *l3,
					struct l4srv *srv,unsigned emph){
	l4obj *l4;

	// FIXME we ought be able to use this assert (or preferably get rid of
	// l3->l2 entirely), but it's being triggered (see bug 415). until we
	// get that resolved, we use the bogon check....
	// assert(l3->l2 == l2);
	if(l3->l2 != l2){
		return NULL;
	}
	if( (l4 = get_l4obj(srv,emph)) ){
		l4obj **prev = &l3->l4objs;

		if(*prev == NULL){
			++is->srvs;
			if(is->expansion >= EXPANSION_SERVICES){
				++l3->l2->lnes;
			}
		}else do{
			struct l4srv *c = (*prev)->l4;

			if(l4_getproto(srv) < l4_getproto(c)){
				break;
			}else if(l4_getproto(srv) == l4_getproto(c)){
				if(l4_getport(srv) < l4_getport(c)){
					break;
				}else if(l4_getport(srv) == l4_getport(c)){
					if(wcscmp(l4srvstr(srv),l4srvstr(c)) < 0){
						break;
					}
				}
			}
		}while( *(prev = &(*prev)->next) );
		l4->next = *prev;
		*prev = l4;
	}
	assert(node_lines(is->expansion,l3->l2) == l3->l2->lnes);
	assert(node_lines(is->expansion,l2) == l2->lnes);
	return l4;
}

static void
print_host_services(WINDOW *w,const interface *i,const l3obj *l,int *line,
			int rows,int cols,wchar_t selectchar,int attrs,
			int minline,int active){
	const struct l4obj *l4;
	const wchar_t *srv;
	int n;

	if(*line >= rows){
		return;
	}
	if(*line < minline){
		*line += !!l->l4objs;
		return;
	}
	--cols;
	n = 0;
	for(l4 = l->l4objs ; l4 ; l4 = l4->next){
		if(l4->emph){
			wattrset(w,attrs | A_BOLD);
		}else{
			wattrset(w,attrs);
		}
		srv = l4srvstr(l4->l4);
		if(n){
			if((unsigned)cols < 1 + wcslen(srv)){ // one for space
				break;
			}
			cols -= 1 + wcslen(srv);
			n += 1 + wcslen(srv);
			wprintw(w," %ls",srv);
		}else{
			if((unsigned)cols < 2 + 5 + wcslen(srv)){ // two for borders
				break;
			}
			cols -= 5 + wcslen(srv);
			n += 5 + wcslen(srv);
			mvwprintw(w,*line,0,"%lc    %ls",selectchar,srv);
			++*line;
		}
	}
	if(n && cols > 0){
		wprintw(w,"%-*.*s",cols,cols,"");
	}
	draw_right_vline(i,active,w);
}

// line: line on which this node starts, within the WINDOW w of {rows x cols}
static void
print_iface_host(const interface *i,const iface_state *is,WINDOW *w,
		const l2obj *l,int line,int rows,int cols,int selected,
		int minline,unsigned endp,int active){
	int aattrs,al3attrs,arattrs,attrs,l3attrs,rattrs,sattrs;
	const wchar_t *devname;
	wchar_t selectchar;
	char legend;
	l3obj *l3;

	// lines_for_interface() counts up nodes, hosts, and up to one line of
	// services per host. if we're a partial, we won't be displaying the
	// first or last (or both) lines of this output. each line that *would*
	// be printed increases 'line'. don't print if line doesn't make up for
	// the degree of top-partialness (line >= 0), but continue. break once
	// line is greater than the last available line, since we won't print
	// anymore.
	if(line >= rows - !endp){
		return;
	}
	switch(l->cat){
		case RTN_UNICAST:
			attrs = COLOR_PAIR(UCAST_COLOR);
			l3attrs = COLOR_PAIR(UCAST_L3_COLOR);
			rattrs = COLOR_PAIR(UCAST_RES_COLOR);
			sattrs = COLOR_PAIR(USELECTED_COLOR);
			aattrs = COLOR_PAIR(UCAST_ALTROW_COLOR);
			al3attrs = COLOR_PAIR(UCAST_ALTROW_L3_COLOR);
			arattrs = COLOR_PAIR(UCAST_ALTROW_RES_COLOR);
			devname = get_devname(l->l2);
			legend = 'U';
			break;
		case RTN_LOCAL:
			attrs = COLOR_PAIR(LCAST_COLOR) | OUR_BOLD;
			l3attrs = COLOR_PAIR(LCAST_L3_COLOR) | OUR_BOLD;
			rattrs = COLOR_PAIR(LCAST_RES_COLOR) | OUR_BOLD;
			sattrs = COLOR_PAIR(LSELECTED_COLOR);
			aattrs = COLOR_PAIR(LCAST_ALTROW_COLOR) | OUR_BOLD;
			al3attrs = COLOR_PAIR(LCAST_ALTROW_L3_COLOR) | OUR_BOLD;
			arattrs = COLOR_PAIR(LCAST_ALTROW_RES_COLOR) | OUR_BOLD;
			if(interface_virtual_p(i) ||
				(devname = get_devname(l->l2)) == NULL){
				devname = i->topinfo.devname;
			}
			legend = 'L';
			break;
		case RTN_MULTICAST:
			attrs = COLOR_PAIR(MCAST_COLOR);
			l3attrs = COLOR_PAIR(MCAST_L3_COLOR);
			rattrs = COLOR_PAIR(MCAST_RES_COLOR);
			sattrs = COLOR_PAIR(MSELECTED_COLOR);
			aattrs = COLOR_PAIR(MCAST_ALTROW_COLOR);
			al3attrs = COLOR_PAIR(MCAST_ALTROW_L3_COLOR);
			arattrs = COLOR_PAIR(MCAST_ALTROW_RES_COLOR);
			devname = get_devname(l->l2);
			legend = 'M';
			break;
		case RTN_BROADCAST:
			attrs = COLOR_PAIR(BCAST_COLOR);
			l3attrs = COLOR_PAIR(BCAST_L3_COLOR);
			rattrs = COLOR_PAIR(BCAST_RES_COLOR);
			sattrs = COLOR_PAIR(BSELECTED_COLOR);
			aattrs = COLOR_PAIR(BCAST_ALTROW_COLOR);
			al3attrs = COLOR_PAIR(BCAST_ALTROW_L3_COLOR);
			arattrs = COLOR_PAIR(BCAST_ALTROW_RES_COLOR);
			devname = get_devname(l->l2);
			legend = 'B';
			break;
		default:
			assert(0 && "Unknown l2 category");
			break;
	}
	if(selected){
		aattrs = attrs = sattrs;
		al3attrs = l3attrs = sattrs;
		arattrs = rattrs = sattrs;
		selectchar = l->l3objs && is->expansion >= EXPANSION_HOSTS
				? L'⎧' : L'⎨';
	}else{
		selectchar = L' ';
	}
	if(!interface_up_p(i)){
		attrs = (attrs & A_BOLD) | COLOR_PAIR(BULKTEXT_COLOR);
		l3attrs = (l3attrs & A_BOLD) | COLOR_PAIR(BULKTEXT_COLOR);
		rattrs = (rattrs & A_BOLD) | COLOR_PAIR(BULKTEXT_COLOR);
		aattrs = (aattrs & A_BOLD) | COLOR_PAIR(BULKTEXT_ALTROW_COLOR);
		al3attrs = (al3attrs & A_BOLD) | COLOR_PAIR(BULKTEXT_ALTROW_COLOR);
		arattrs = (arattrs & A_BOLD) | COLOR_PAIR(BULKTEXT_ALTROW_COLOR);
	}
	wattrset(w,!(line % 2) ? attrs : aattrs);
	if(line >= minline){
		char hw[HWADDRSTRLEN(i->addrlen)];
		int len;

		l2ntop(l->l2, i->addrlen, hw);
		if(devname){
			// FIXME this doesn't properly account for multicolumn
			// characters in the devname,  including tabs
			len = cols - PREFIXCOLUMNS * 2 - 5 - HWADDRSTRLEN(i->addrlen);
			if(!interface_up_p(i)){
				len += PREFIXCOLUMNS * 2 + 1;
			}
			mvwprintw(w, line, 0, "%lc%c %s %-*.*ls",
				selectchar, legend, hw, len, len, devname);
		}else{
			len = cols - PREFIXCOLUMNS * 2 - 5;
			if(!interface_up_p(i)){
				len += PREFIXCOLUMNS * 2 + 1;
			}
			mvwprintw(w, line, 0, "%lc%c %-*.*s",
				selectchar, legend, len, len, hw);
		}
		if(interface_up_p(i)){
			char dbuf[PREFIXCOLUMNS + 1];
			if(get_srcpkts(l->l2) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
        qprefix(get_dstpkts(l->l2), 1, dbuf,  1);
				wprintw(w, "%-*.*s%*s", PREFIXCOLUMNS + 1, PREFIXCOLUMNS + 1, "", PREFIXFMT(dbuf));
			}else{
				char sbuf[PREFIXCOLUMNS + 1];
        qprefix(get_srcpkts(l->l2), 1, sbuf,  1);
				qprefix(get_dstpkts(l->l2), 1, dbuf,  1);
				wprintw(w, "%*s %*s",  PREFIXFMT(sbuf), PREFIXFMT(dbuf));
			}
		}
		draw_right_vline(i,active,w);
	}
	++line;
	if(is->expansion >= EXPANSION_HOSTS){
		for(l3 = l->l3objs ; l3 ; l3 = l3->next){
			char nw[INET6_ADDRSTRLEN + 1]; // FIXME
			const wchar_t *name;

			if(selectchar != L' '){
				if(l3->next || (l3->l4objs && is->expansion >= EXPANSION_SERVICES)){
					selectchar = L'⎪';
				}else{
					selectchar = L'⎩';
				}
			}
			if(line >= rows - !endp){
				break;
			}
			if(line >= minline){
				int len,wlen;

				wattrset(w,!(line % 2) ? l3attrs : al3attrs);
				l3ntop(l3->l3,nw,sizeof(nw));
				if((name = get_l3name(l3->l3)) == NULL){
					name = L"";
				}
				mvwprintw(w,line,0,"%lc   %s ", selectchar,nw)
				wattrset(w,!(line % 2) ? rattrs : arattrs);
				len = cols - PREFIXCOLUMNS * 2 - 7 - strlen(nw);
				wlen = len - wcswidth(name,wcslen(name));
				if(wlen < 0){
					wlen = 0;
				}
				wprintw(w,"%.*ls%*.*s",len,name,wlen,wlen,"");
				wattrset(w,!(line % 2) ? l3attrs : al3attrs);
				{
					char sbuf[PREFIXSTRLEN + 1];
					char dbuf[PREFIXSTRLEN + 1];
					if(l3_get_srcpkt(l3->l3) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
            qprefix(l3_get_dstpkt(l3->l3), 1, dbuf,  1);
						wprintw(w, "%-*.*s%*s", PREFIXCOLUMNS + 1, PREFIXCOLUMNS + 1,
								"", PREFIXFMT(dbuf));
					}else{
						qprefix(l3_get_srcpkt(l3->l3), 1, sbuf,  1);
					  qprefix(l3_get_dstpkt(l3->l3), 1, dbuf,  1);
						wprintw(w, "%*s %*s", PREFIXFMT(sbuf), PREFIXFMT(dbuf));
					}
				}
				draw_right_vline(i,active,w);
			}
			++line;
			if(is->expansion >= EXPANSION_SERVICES){
				if(selectchar != L' ' && !l3->next){
					selectchar = L'⎩';
				}
				print_host_services(w,i,l3,&line,rows - !endp,
					cols,selectchar,
					!(line % 2) ? attrs : aattrs,
					minline,active);
			}
		}
	}
}

// FIXME all these casts! :/ appalling
static void
print_iface_hosts(const interface *i,const iface_state *is,const reelbox *rb,
				WINDOW *w,int rows,int cols,
				unsigned topp,unsigned endp,int active){
	// If the interface is down, we don't lead with the summary line
	const int sumline = !!interface_up_p(i);
	const struct l2obj *cur;
	long line;

	if(is->expansion < EXPANSION_NODES){
		return;
	}
	// First, print the selected interface (if there is one)
	cur = rb->selected;
	line = rb->selline + sumline;
	while(cur && line + (long)cur->lnes >= !!topp + sumline){
		print_iface_host(i,is,w,cur,line,rows,cols,cur == rb->selected,
					!!topp + sumline,endp,active);
		// here we traverse, then account...
		if( (cur = cur->prev) ){
			line -= cur->lnes;
		}
	}
	line = rb->selected ? (rb->selline + (long)rb->selected->lnes) :
						-(long)topp + 1;
	line += sumline;
	cur = (rb->selected ? rb->selected->next : is->l2objs);
	while(cur && line < rows){
		print_iface_host(i,is,w,cur,line,rows,cols,0,0,endp,active);
		// here, we account before we traverse. this is correct.
		line += cur->lnes;
		cur = cur->next;
	}
}

static int
iface_optstr(WINDOW *w,const char *str,int hcolor,int bcolor){
	if(wcolor_set(w,bcolor,NULL) != 0){
		return -1;
	}
	if(waddch(w,'|') == ERR){
		return -1;
	}
	if(wcolor_set(w,hcolor,NULL) != 0){
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

// Abovetop: lines hidden at the top of the screen
// Belowend: lines hidden at the bottom of the screen
static void
iface_box(const interface *i,const iface_state *is,WINDOW *w,int active,
				unsigned abovetop,unsigned belowend){
	int bcolor,hcolor,rows,cols;
	size_t buslen;
	int attrs;

	getmaxyx(w,rows,cols);
	bcolor = interface_up_p(i) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = interface_up_p(i) ? UHEADING_COLOR : DHEADING_COLOR;
	if(abovetop == 0){
		attrs = active ? A_REVERSE : A_BOLD;
		wattrset(w,attrs | COLOR_PAIR(bcolor));
		bevel_top(w);
		wattroff(w,A_REVERSE);
		if(active){
			wattron(w,A_BOLD);
		}
		mvwprintw(w,0,0,"[");
		wcolor_set(w,hcolor,NULL);
		if(active){
			wattron(w,A_BOLD);
		}else{
			wattroff(w,A_BOLD);
		}
		waddstr(w,i->name);
		wprintw(w," (%s",is->typestr);
		if(strlen(i->drv.driver)){
			waddch(w,' ');
			waddstr(w,i->drv.driver);
			if(strlen(i->drv.version)){
				wprintw(w," %s",i->drv.version);
			}
			if(strlen(i->drv.fw_version)){
				wprintw(w," fw %s",i->drv.fw_version);
			}
		}
		waddch(w,')');
		wcolor_set(w,bcolor,NULL);
		if(active){
			wattron(w,A_BOLD);
		}
		wprintw(w,"]");
		wmove(w,0,cols - 4);
		wattron(w,A_BOLD);
		waddwstr(w,is->expansion == EXPANSION_MAX ? L"[-]" :
					      is->expansion == 0 ? L"[+]" : L"[±]");
		wattron(w,attrs);
		wattroff(w,A_REVERSE);
	}
	if(belowend == 0){
		attrs = active ? A_REVERSE : A_BOLD;
		wattrset(w,attrs | COLOR_PAIR(bcolor));
		bevel_bottom(w);
		wattroff(w,A_REVERSE);
		if(active){
			wattron(w,A_BOLD);
		}
		mvwprintw(w,rows - 1,2,"[");
		wcolor_set(w,hcolor,NULL);
		if(active){
			wattron(w,A_BOLD);
		}else{
			wattroff(w,A_BOLD);
		}
		wprintw(w,"mtu %d",i->mtu);
		if(interface_up_p(i)){
			char buf[U64STRLEN + 1];

			iface_optstr(w,"up",hcolor,bcolor);
			if(i->settings_valid == SETTINGS_VALID_ETHTOOL){
				if(!interface_carrier_p(i)){
					waddstr(w," (no carrier)");
				}else{
					wprintw(w, " (%sb %s)", qprefix(i->settings.ethtool.speed * (uint64_t)1000000lu, 1, buf,  1),
								duplexstr(i->settings.ethtool.duplex));
				}
			}else if(i->settings_valid == SETTINGS_VALID_WEXT){
				if(i->settings.wext.mode == NL80211_IFTYPE_MONITOR){
					wprintw(w," (%s",modestr(i->settings.wext.mode));
				}else if(!interface_carrier_p(i)){
					wprintw(w," (%s, no carrier",modestr(i->settings.wext.mode));
				}else{
					wprintw(w, " (%sb %s", qprefix(i->settings.wext.bitrate, 1, buf,  1),
								modestr(i->settings.wext.mode));
				}
				if(i->settings.wext.freq >= MAX_WIRELESS_CHANNEL){
					wprintw(w," %sHz)", qprefix(i->settings.wext.freq, 1, buf,  1));
				}else if(i->settings.wext.freq){
					wprintw(w," ch %ju)",i->settings.wext.freq);
				}else{
					wprintw(w,")");
				}
			}else if(i->settings_valid == SETTINGS_VALID_NL80211){
				if(i->settings.nl80211.mode == NL80211_IFTYPE_MONITOR){
					wprintw(w," (%s",modestr(i->settings.nl80211.mode));
				}else if(!interface_carrier_p(i)){
					wprintw(w," (%s, no carrier",modestr(i->settings.nl80211.mode));
				}else{
					wprintw(w, " (%sb %s", qprefix(i->settings.nl80211.bitrate, 1, buf,  1),
								modestr(i->settings.nl80211.mode));
				}
				if(i->settings.nl80211.freq >= MAX_WIRELESS_CHANNEL){
					wprintw(w," %sHz)", qprefix(i->settings.nl80211.freq, 1, buf,  1));
				}else if(i->settings.nl80211.freq){
					wprintw(w," ch %ju)",i->settings.nl80211.freq);
				}else{
					wprintw(w,")");
				}
			}
		}else{
			iface_optstr(w,"down",hcolor,bcolor);
			if(i->settings_valid == SETTINGS_VALID_WEXT){
				wprintw(w," (%s)",modestr(i->settings.wext.mode));
			}
		}
		if(interface_promisc_p(i)){
			iface_optstr(w,"promisc",hcolor,bcolor);
		}
		wcolor_set(w,bcolor,NULL);
		if(active){
			wattron(w,A_BOLD);
		}
		wprintw(w,"]");
		if( (buslen = strlen(i->drv.bus_info)) ){
			if(active){
				// FIXME Want the text to be bold -- currently unreadable
				wattrset(w,A_REVERSE | COLOR_PAIR(bcolor));
			}else{
				wattrset(w,COLOR_PAIR(bcolor) | A_BOLD);
			}
			if(i->busname){
				buslen += strlen(i->busname) + 1;
				mvwprintw(w,rows - 1,cols - (buslen + 2),
						"%s:%s",i->busname,i->drv.bus_info);
			}else{
				mvwprintw(w,rows - 1,cols - (buslen + 2),
						"%s",i->drv.bus_info);
			}
		}
	}
}

static void
print_iface_state(const interface *i,const iface_state *is,WINDOW *w,
			int rows,int cols,unsigned topp,int active){
	char buf[U64STRLEN + 1],buf2[U64STRLEN + 1];
	unsigned long usecdomain;

	if(rows < 2 || topp > 1){
		return;
	}
	assert(wattrset(w,A_BOLD | COLOR_PAIR(IFACE_COLOR)) != ERR);
	// FIXME broken if bps domain ever != fps domain. need unite those
	// into one FTD stat by letting it take an object...
	// FIXME this leads to a "ramp-up" period where we approach steady state
	usecdomain = i->bps.usec * i->bps.total;
	assert(mvwprintw(w,!topp,0,"%u node%s. Last %lus: %7sb/s (%sp)",
		is->nodes,is->nodes == 1 ? "" : "s",
		usecdomain / 1000000,
		qprefix(timestat_val(&i->bps) * CHAR_BIT * 1000000 * 100 / usecdomain, 100, buf,  0),
		qprefix(timestat_val(&i->fps), 1, buf2,  1)) != ERR);
	mvwaddstr(w,1,cols - PREFIXCOLUMNS * 2 - 1,"TotSrc  TotDst");
	draw_right_vline(i,active,w);
}

void free_iface_state(iface_state *is){
	l2obj *l2 = is->l2objs;

	while(l2){
		l2obj *tmp = l2->next;
		free_l2obj(l2);
		l2 = tmp;
	}
}

int redraw_iface(const reelbox *rb, int active){
	const iface_state *is = rb->is;
	const interface *i = is->iface;
	int rows,cols,scrrows;
	unsigned topp,endp;

	if(panel_hidden(rb->panel)){
		return 0;
	}
	scrrows = getmaxy(stdscr);
	if(iface_wholly_visible_p(scrrows,rb) || active){ // completely visible
		topp = endp = 0;
	}else if(getbegy(rb->subwin) == 0){ // no top
		topp = iface_lines_unbounded(is) - getmaxy(rb->subwin);
		endp = 0;
	}else{
		topp = 0;
		endp = 1; // no bottom FIXME
	}
	getmaxyx(rb->subwin,rows,cols);
	werase(rb->subwin);
	iface_box(i,is,rb->subwin,active,topp,endp);
	print_iface_hosts(i,is,rb,rb->subwin,rows,cols,topp,endp,active);
	if(interface_up_p(i)){
		print_iface_state(i,is,rb->subwin,rows,cols,topp,active);
	}
	return 0;
}

// Move this interface, possibly hiding it. Negative delta indicates movement
// up, positive delta moves down. rows and cols describe the containing window.
void move_interface(reelbox *rb,int targ,int rows,int cols,int delta,int active){
	const iface_state *is;
	int nlines,rr;
       
	is = rb->is;
	//fprintf(stderr,"  moving %s (%d) from %d to %d (%d)\n",is->iface->name,
	//		iface_lines_bounded(is,rows),getbegy(rb->subwin),targ,delta);
	assert(rb->is);
	assert(rb->is->rb == rb);
	werase(rb->subwin);
	screen_update();
	if(iface_wholly_visible_p(rows,rb)){
		move_panel(rb->panel,targ,1);
		if(getmaxy(rb->subwin) != iface_lines_bounded(is,rows)){
			wresize(rb->subwin,iface_lines_bounded(is,rows),PAD_COLS(cols));
			if(panel_hidden(rb->panel)){
				show_panel(rb->panel);
			}
		}
		redraw_iface(rb,active);
		return;
	}
	rr = getmaxy(rb->subwin);
	if(delta > 0){ // moving down
		if(targ >= rows - 1){
			hide_panel(rb->panel);
			return;
		}
		nlines = rows - targ - 1; // sans-bottom partial
	}else{
		if((rr + getbegy(rb->subwin)) <= -delta){
			hide_panel(rb->panel);
			return;
		}
		if(targ < 0){
			nlines = rr + targ;
			targ = 0;
		}else{
			nlines = iface_lines_bounded(is,rows - targ + 1);
		}
	}
	if(nlines < 1){
		hide_panel(rb->panel);
		return;
	}else if(nlines > rr){
		move_panel(rb->panel,targ,1);
		wresize(rb->subwin,nlines,PAD_COLS(cols));
	}else if(nlines < rr){
		wresize(rb->subwin,nlines,PAD_COLS(cols));
		move_panel(rb->panel,targ,1);
	}else{
		move_panel(rb->panel,targ,1);
	}
	redraw_iface(rb,active);
	show_panel(rb->panel);
	return;
}

// This is the number of lines we'd have in an optimal world; we might have
// fewer available to us on this screen at this time.
int lines_for_interface(const iface_state *is){
	int lnes = 2 + interface_up_p(is->iface);

	switch(is->expansion){ // Intentional fallthrus
		case EXPANSION_SERVICES:
			lnes += is->srvs;
			/* intentional fallthrough */
		case EXPANSION_HOSTS:
			lnes += is->hosts;
			/* intentional fallthrough */
		case EXPANSION_NODES:
			lnes += is->nodes;
			lnes += is->vnodes;
			/* intentional fallthrough */
		case EXPANSION_NONE:
			return lnes;
	}
	assert(0);
	return -1;
}

// Is the interface window entirely visible? We can't draw it otherwise, as it
// will obliterate the global bounding box.
int iface_wholly_visible_p(int rows,const reelbox *rb){
	const iface_state *is = rb->is;

	// return iface_lines_bounded(is,rows) <= getmaxy(rb->subwin);
	if(rb->scrline + iface_lines_bounded(is,rows) >= rows){
		return 0;
	}else if(rb->scrline < 0){
		return 0;
	}else if(rb->scrline == 0 && iface_lines_bounded(is,rows) != getmaxy(rb->subwin)){
		return 0;
	}
	return 1;
}

// Recompute ->lnes values for all nodes, and return the number of lines of
// output available before and after the current selection. If there is no
// current selection, the return value ought not be ascribed meaning. O(N) on
// the number of l2hosts, not just those visible -- unacceptable! FIXME
static void
recompute_lines(iface_state *is,int *before,int *after){
	int newsel;
	l2obj *l;

	*after = -1;
	*before = -1;
	newsel = !!interface_up_p(is->iface);
	for(l = is->l2objs ; l ; l = l->next){
		l->lnes = node_lines(is->expansion,l);
		if(l == is->rb->selected){
			*before = newsel;
			*after = l->lnes ? l->lnes - 1 : 0;
		}else if(*after >= 0){
			*after += l->lnes;
		}else{
			newsel += l->lnes;
		}
	}
}

// When we expand or collapse, we want the current selection to contain above
// it approximately the same proportion of the entire interface. That is, if
// we're at the top, we ought remain so; if we're at the bottom, we ought
// remain so; if we fill the entire screen before and after the operation, we
// oughtn't move more than a few rows at the most.
//
// oldsel: old line of the selection, within the window
// oldrows: old number of rows in the iface
// newrows: new number of rows in the iface
// oldlines: number of lines selection used to occupy
void recompute_selection(iface_state *is,int oldsel,int oldrows,int newrows){
	int newsel,bef,aft;

	// Calculate the maximum new line -- we can't leave space at the top or
	// bottom, so we can't be after the true number of lines of output that
	// precede us, or before the true number that follow us.
	recompute_lines(is,&bef,&aft);
	if(bef < 0 || aft < 0){
		assert(!is->rb->selected);
		return;
	}
	// Account for lost/restored lines within the selection. Negative means
	// we shrank, positive means we grew, 0 stayed the same.
	// Calculate the new target line for the selection
	newsel = oldsel * newrows / oldrows;
	if(oldsel * newrows % oldrows >= oldrows / 2){
		++newsel;
	}
	// If we have a full screen's worth after us, we can go anywhere
	if(newsel > bef){
		newsel = bef;
	}
	/*wstatus_locked(stdscr,"newsel: %d bef: %d aft: %d oldsel: %d maxy: %d",
			newsel,bef,aft,oldsel,getmaxy(is->rb->subwin));
	update_panels();
	doupdate();*/
	if(newsel + aft <= getmaxy(is->rb->subwin) - 2 - !!interface_up_p(is->iface)){
		newsel = getmaxy(is->rb->subwin) - aft - 2 - !!interface_up_p(is->iface);
	}
	if(newsel + (int)node_lines(is->expansion,is->rb->selected) >= getmaxy(is->rb->subwin) - 2){
		newsel = getmaxy(is->rb->subwin) - 2 - node_lines(is->expansion,is->rb->selected);
	}
	/*wstatus_locked(stdscr,"newsel: %d bef: %d aft: %d oldsel: %d maxy: %d",
			newsel,bef,aft,oldsel,getmaxy(is->rb->subwin));
	update_panels();
	doupdate();*/
	if(newsel){
		is->rb->selline = newsel;
	}
	assert(is->rb->selline >= 1);
	assert(is->rb->selline < getmaxy(is->rb->subwin) - 1 || !is->expansion);
}
