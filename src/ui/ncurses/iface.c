#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <sys/socket.h>
#include <linux/version.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <ui/ncurses/util.h>
#include <omphalos/hwaddrs.h>
#include <ncursesw/ncurses.h>
#include <ui/ncurses/iface.h>
#include <omphalos/netaddrs.h>
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

iface_state *create_interface_state(interface *i){
	iface_state *ret;
	const char *tstr;

	if( (tstr = lookup_arptype(i->arptype,NULL)) ){
		if( (ret = malloc(sizeof(*ret))) ){
			ret->hosts = ret->nodes = 0;
			ret->l2objs = NULL;
			ret->devaction = 0;
			ret->typestr = tstr;
			ret->lastprinted.tv_sec = ret->lastprinted.tv_usec = 0;
			ret->iface = i;
		}
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

void print_iface_hosts(const interface *i,const iface_state *is){
	int rows,cols,line;
	const l2obj *l;

	getmaxyx(is->subwin,rows,cols);
	cols -= 2 + 3 + HWADDRSTRLEN(i->addrlen);
	assert(cols >= 0);
	assert(rows);
	// If the interface is down, we don't lead with the summary line
	line = !!interface_up_p(i);
	for(l = is->l2objs ; l ; l = l->next){
		char hw[HWADDRSTRLEN(i->addrlen)];
		const char *devname;
		char legend;
		l3obj *l3;
		
		if(++line + 1 >= rows){
			break;
		}
		switch(l->cat){
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
		if(!interface_up_p(i)){
			assert(wcolor_set(is->subwin,DBORDER_COLOR,NULL) != ERR);
		}
		l2ntop(l->l2,i->addrlen,hw);
		if((devname = get_devname(l->l2)) == NULL){
			if(l->cat == RTN_LOCAL){
				devname = i->topinfo.devname;
			}
		}
		if(devname){
			int len = strlen(devname);

			if(len > cols){
				len = cols;
			}
			assert(mvwprintw(is->subwin,line,1," %c %s %.*s",
				legend,hw,cols - 1,devname) != ERR);
		}else{
			assert(mvwprintw(is->subwin,line,1," %c %s",legend,hw) != ERR);
		}
		for(l3 = l->l3objs ; l3 ; l3 = l3->next){
			char nw[INET6_ADDRSTRLEN + 1]; // FIXME

			if(++line + 1 >= rows){
				break;
			}
			l3ntop(l3->l3,nw,sizeof(nw));
			assert(mvwprintw(is->subwin,line,1,"    %s",nw) != ERR);
		}
		/*
		const char *nname;
		int hlen = strlen(nname);
		if((nname = get_name(l->l2)) == NULL){
			nname = "";
		}
		*/
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

// to be called only while ncurses lock is held
void iface_box(WINDOW *w,const interface *i,const iface_state *is,int active){
	int bcolor,hcolor,scrrows,scrcols;
	size_t buslen;
	int attrs;

	getmaxyx(w,scrrows,scrcols);
	bcolor = interface_up_p(i) ? UBORDER_COLOR : DBORDER_COLOR;
	hcolor = interface_up_p(i) ? UHEADING_COLOR : DHEADING_COLOR;
	attrs = active ? A_REVERSE : A_BOLD;
	assert(wattrset(w,attrs | COLOR_PAIR(bcolor)) == OK);
	assert(bevel(w) == OK);
	assert(wattroff(w,A_REVERSE) == OK);
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
	assert(wattron(w,attrs) != ERR);
	assert(wattroff(w,A_REVERSE) != ERR);
	assert(mvwprintw(w,scrrows - 1,2,"[") != ERR);
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
			assert(wattrset(w,A_REVERSE | COLOR_PAIR(bcolor)) != ERR);
		}else{
			assert(wattrset(w,COLOR_PAIR(bcolor) | A_BOLD) != ERR);
		}
		if(i->busname){
			buslen += strlen(i->busname) + 1;
			assert(mvwprintw(w,scrrows - 1,scrcols - (buslen + 2),
					"%s:%s",i->busname,i->drv.bus_info) != ERR);
		}else{
			assert(mvwprintw(w,scrrows - 1,scrcols - (buslen + 2),
					"%s",i->drv.bus_info) != ERR);
		}
	}
}

void print_iface_state(const interface *i,const iface_state *is){
	char buf[U64STRLEN + 1],buf2[U64STRLEN + 1];
	unsigned long usecdomain;

	assert(wattrset(is->subwin,A_BOLD | COLOR_PAIR(IFACE_COLOR)) != ERR);
	// FIXME broken if bps domain ever != fps domain. need unite those
	// into one FTD stat by letting it take an object...
	// FIXME this leads to a "ramp-up" period where we approach steady state
	usecdomain = i->bps.usec * i->bps.total;
	assert(mvwprintw(is->subwin,1,1,"Last %lus: %7sb/s (%sp) Nodes: %-5u",
				usecdomain / 1000000,
				prefix(timestat_val(&i->bps) * CHAR_BIT * 1000000 * 100 / usecdomain,100,buf,sizeof(buf),0),
				prefix(timestat_val(&i->fps),1,buf2,sizeof(buf2),1),
				is->nodes) != ERR);
}

void free_iface_state(iface_state *is){
	l2obj *l2 = is->l2objs;

	while(l2){
		l2obj *tmp = l2->next;
		free_l2obj(l2);
		l2 = tmp;
	}
}

void redraw_iface(const interface *i,const struct iface_state *is,int active){
	assert(werase(is->subwin) != ERR);
	iface_box(is->subwin,i,is,active);
	if(interface_up_p(i)){
		print_iface_state(i,is);
	}
	print_iface_hosts(i,is);
}

// Move this interface, possibly hiding it or bringing it onscreen. Negative
// delta indicates movement up, positive delta moves down. Returns a non-zero
// if the interface is active and would be pushed offscreen.
int move_interface(iface_state *is,int rows,int delta,int active){
	is->scrline += delta;
	if(iface_visible_p(rows,is)){
		interface *ii = is->iface;

		assert(move_panel(is->panel,is->scrline,1) != ERR);
		redraw_iface(ii,is,active);
	// use "will_be_visible" as "would_be_visible" here, heh
	}else if(!panel_hidden(is->panel)){
		if(active){
			is->scrline -= delta;
			return -1;
		}
		// FIXME see if we can shrink it first!
		assert(werase(is->subwin) != ERR);
		assert(hide_panel(is->panel) != ERR);
	}
	return 0;
}

// This is the number of lines we'd have in an optimal world; we might have
// fewer available to us on this screen at this time.
int lines_for_interface(const interface *i,const iface_state *is){
	return 2 + is->nodes + is->hosts + interface_up_p(i);
}

// Is the interface window entirely visible? We can't draw it otherwise, as it
// will obliterate the global bounding box.
int iface_visible_p(int rows,const iface_state *is){
	if(is->scrline + lines_for_interface(is->iface,is) >= rows){
		return 0;
	}else if(is->scrline < 1){
		return 0;
	}
	return 1;
}
