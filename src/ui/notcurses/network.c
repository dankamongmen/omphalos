#include <string.h>
#include <stdlib.h>
#include <omphalos/resolv.h>
#include <omphalos/procfs.h>
#include <ui/notcurses/util.h>
#include <ui/notcurses/core.h>
#include <ui/notcurses/network.h>

static const wchar_t *
state3str(int state){
	if(state == 0){
		return L"Off";
	}else if(state > 0){
		return L"On";
	}
	return L"Unknown";
}

static wchar_t
state3char(int state){
	if(state == 0){
		return L'-';
	}else if(state > 0){
		return L'+';
	}
	return L'?';
}

#define NETWORKROWS 4

int update_network_details(struct ncplane *w){
	const int col = START_COL;
	const int row = 1;
	procfs_state ps;
	int r,c,z;

	assert(wattrset(w,SUBDISPLAY_ATTR) == 0);
	if(get_procfs_state(&ps)){
		return -1;
	}
	getmaxyx(w,r,c);
	assert(c > col);
	if((z = r) >= NETWORKROWS){
		z = NETWORKROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (NETWORKROWS - 1):{
		assert(mvwprintw(w,row + z,col,"TCP [%s]: SAck%lc DSAck%lc FAck%lc FRTO%lc",
					ps.tcp_ccalg,state3char(ps.tcp_sack),
					state3char(ps.tcp_dsack),
					state3char(ps.tcp_fack),
					state3char(ps.tcp_frto)) != -1);
		--z;
	}	/* intentional fallthrough */
	case 2:{
		char *dns = stringize_resolvers();

		if(dns){
			assert(mvwprintw(w,row + z,col,"DNS: %s",dns) != -1);
			free(dns);
		}else{
			assert(mvwprintw(w,row + z,col,"No DNS servers configured") != -1);
		}
		--z;
	}	/* intentional fallthrough */
	case 1:{
		assert(mvwprintw(w,row + z,col,"Forwarding: IPv4%lc IPv6%lc   Reverse path filtering: %ls",
					state3char(ps.ipv4_forwarding),
					state3char(ps.ipv6_forwarding),
					state3str(ps.rp_filter)) != -1);
		--z;
	}	/* intentional fallthrough */
	case 0:{
		assert(mvwprintw(w,row + z,col,"Proxy ARP: %ls",state3str(ps.proxyarp)) != -1);
		--z;
		break;
	}default:{
		return -1;
	} }
	return 0;
}

int display_network_locked(struct ncplane *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,NETWORKROWS,0,L"press 'n' to dismiss display")){
		goto err;
	}
	update_network_details(panel_window(ps->p));
	return 0;

err:
	if(ps->p){
		struct ncplane *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}

#define BRIDGEROWS 1

int display_bridging_locked(struct ncplane *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,BRIDGEROWS,0,L"press 'b' to dismiss display")){
		goto err;
	}
	//update_bridge_details(panel_window(ps->p));
	return 0;

err:
	if(ps->p){
		struct ncplane *psw = panel_window(ps->p);

		hide_panel(ps->p);
		del_panel(ps->p);
		delwin(psw);
	}
	memset(ps,0,sizeof(*ps));
	return -1;
}
