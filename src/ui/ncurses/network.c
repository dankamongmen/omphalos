#include <string.h>
#include <omphalos/procfs.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/network.h>

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

#define NETWORKROWS 3

static int
update_network_details(WINDOW *w){
	const int col = START_COL;
	const int row = 1;
	procfs_state ps;
	int r,c,z;

	assert(wattrset(w,SUBDISPLAY_ATTR) == OK);
	if(get_procfs_state(&ps)){
		return ERR;
	}
	getmaxyx(w,r,c);
	assert(c > col);
	if((z = r) >= NETWORKROWS){
		z = NETWORKROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (NETWORKROWS - 1):{
		assert(mvwprintw(w,row + z,col,"TCP [%s]: SACK%lc DSACK%lc FACK%lc FRTO%lc",
					ps.tcp_ccalg,state3char(ps.tcp_sack),
					state3char(ps.tcp_dsack),
					state3char(ps.tcp_fack),
					state3char(ps.tcp_frto)) != ERR);
		--z;
	}case 1:{
		assert(mvwprintw(w,row + z,col,"Forwarding: IPv4%lc IPv6%lc",
					state3char(ps.ipv4_forwarding),
					state3char(ps.ipv6_forwarding)) != ERR);
		--z;
	}case 0:{
		assert(mvwprintw(w,row + z,col,"Proxy ARP: %ls",state3str(ps.proxyarp)) != ERR);
		--z;
		break;
	}default:{
		return ERR;
	} }
	return OK;
}

int display_network_locked(WINDOW *mainw,struct panel_state *ps){
	memset(ps,0,sizeof(*ps));
	if(new_display_panel(mainw,ps,NETWORKROWS,0,L"press 'n' to dismiss display")){
		goto err;
	}
	update_network_details(panel_window(ps->p));
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
