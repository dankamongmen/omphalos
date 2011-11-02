#include <string.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/network.h>

// FIXME get this from omphalos proper
static int ipv4_forwarding,ipv6_forwarding,proxyarp;

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

#define NETWORKROWS 2	// FIXME

static int
update_network_details(WINDOW *w){
	const int col = START_COL;
	const int row = 1;
	int r,c,z;

	getmaxyx(w,r,c);
	assert(c > col);
	if((z = r) >= NETWORKROWS){
		z = NETWORKROWS - 1;
	}
	switch(z){ // Intentional fallthroughs all the way to 0
	case (NETWORKROWS - 1):{
		assert(mvwprintw(w,row + z,col,"Forwarding defaults: IPv4%lc IPv6%lc",
					state3char(ipv4_forwarding),
					state3char(ipv6_forwarding)) != ERR);
		--z;
	}case 0:{
		assert(mvwprintw(w,row + z,col,"Proxy ARP default: %ls",state3str(proxyarp)) != ERR);
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
