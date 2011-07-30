#include <linux/version.h>
#include <linux/nl80211.h>
#include <ui/ncurses/util.h>
#include <ncursesw/ncurses.h>
#include <ui/ncurses/iface.h>
#include <omphalos/interface.h>

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
int iface_box(WINDOW *w,const interface *i,const iface_state *is,int active){
	int bcolor,hcolor,scrrows,scrcols;
	size_t buslen;
	int attrs;

	getmaxyx(w,scrrows,scrcols);
	assert(scrrows); // FIXME
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
	assert(mvwprintw(w,is->ysize - 1,2,"[") != ERR);
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
			if(!interface_carrier_p(i)){
				if(i->settings.wext.mode != NL80211_IFTYPE_MONITOR){
					assert(wprintw(w," (%s, no carrier)",modestr(i->settings.wext.mode)) != ERR);
				}else{
					assert(wprintw(w," (%s)",modestr(i->settings.wext.mode)) != ERR);
				}
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
			assert(mvwprintw(w,is->ysize - 1,scrcols - (buslen + 2),
					"%s:%s",i->busname,i->drv.bus_info) != ERR);
		}else{
			assert(mvwprintw(w,is->ysize - 1,scrcols - (buslen + 2),
					"%s",i->drv.bus_info) != ERR);
		}
	}
	return 0;
}

