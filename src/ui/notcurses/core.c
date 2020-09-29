#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <omphalos/diag.h>
#include <omphalos/ethtool.h>
#include <omphalos/service.h>
#include <ui/notcurses/core.h>
#include <ui/notcurses/util.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <ui/notcurses/iface.h>
#include <omphalos/interface.h>
#include <notcurses/notcurses.h>
#include <ui/notcurses/channels.h>

#define BORDER_COLOR 0x9e9e9e
#define HEADER_COLOR 0xd75fff
#define FOOTER_COLOR 0xffffaf
#define PBORDER_COLOR 0xd787ff
#define PHEADING_COLOR 0xff005f

static iface_state *current_iface;

struct notcurses *NC = NULL;

// Status bar at the bottom of the screen. Must be reallocated upon screen
// resize and allocated based on initial screen at startup. Don't shrink
// it; widening the window again should show the full message.
static char *statusmsg;
static int statuschars;  // True size, not necessarily what's available

int screen_update(void){
  return notcurses_render(NC);
}

static const interface *
get_current_iface(void){
  if(current_iface){
    return current_iface->iface;
  }
  return NULL;
}

int wvstatus_locked(struct ncplane *n, const char *fmt, va_list va){
  assert(statuschars > 0);
  if(fmt == NULL){
    memset(statusmsg, ' ', statuschars - 1);
    statusmsg[statuschars - 1] = '\0';
  }else{
    int n = vsnprintf(statusmsg, statuschars, fmt, va);
    if(n < statuschars){
      memset(statusmsg + n, ' ', statuschars - n - 1);
      statusmsg[statuschars - 1] = '\0';
    }
  }
  return draw_main_window(n);
}

// NULL fmt clears the status bar
int wstatus_locked(struct ncplane *n, const char *fmt, ...){
  va_list va;
  int ret;
  va_start(va, fmt);
  ret = wvstatus_locked(n, fmt, va);
  va_end(va);
  return ret;
}

static int
offload_details(struct ncplane *n, const interface *i, int row, int col,
                const char *name, unsigned val){
  int r = iface_offloaded_p(i, val);
  // these checkboxes don't really look that great at small size
  //return ncplane_printf_yx(w, row, col, "%lc%s", r > 0 ? L'☑' : r < 0 ? L'?' : L'☐', name);
  return ncplane_printf_yx(n, row, col, "%s%c", name,
                           r > 0 ? '+' : r < 0 ? '?' : '-');
}

// Create a panel at the bottom of the window, referred to as the "subdisplay".
// Only one can currently be active at a time. Window decoration and placement
// is managed here; only the rows needed for display ought be provided.
int new_display_panel(struct ncplane *n, struct panel_state *ps, int rows, int cols, const wchar_t *hstr){
  const wchar_t crightstr[] = L"https://nick-black.com/dankwiki/index.php/Omphalos";
  const int crightlen = wcslen(crightstr);
  struct ncplane *psw;
  int x, y;

  ncplane_dim_yx(n, &y, &x);
  if(cols == 0){
    cols = x - START_COL * 2; // indent 2 on the left, 0 on the right
  }else{
    if(x < cols + START_COL * 2){
      wstatus_locked(n, "Screen too small for subdisplay");
      return -1;
    }
  }
  if(y < rows + 3){
    wstatus_locked(n, "Screen too small for subdisplay");
    return -1;
  }
  if(x < crightlen + START_COL * 2){
    wstatus_locked(n, "Screen too small for subdisplay");
    return -1;
  }
  assert((x >= crightlen + START_COL * 2));
  // Keep it one line up from the last display line, so that you get
  // iface summaries (unless you've got a bottom-partial).
  ncplane_options nopts = {
    .y = y - (rows + 4),
    .horiz = {
      .x = x - cols,
    },
    .rows = rows + 2,
    .cols = cols,
  };
  psw = ncplane_create(n, &nopts);
  if(psw == NULL){
    return -1;
  }
  ps->n = psw;
  ps->ysize = rows;
  ncplane_styles_on(psw, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(psw, PBORDER_COLOR);
  bevel(psw);
  ncplane_styles_off(psw, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(psw, PHEADING_COLOR);
  ncplane_putwstr_yx(psw, 0, START_COL * 2, hstr);
  ncplane_putwstr_yx(psw, rows + 1, cols - (crightlen + START_COL * 2), crightstr);
  ncplane_set_base(psw, " ", 0, 0);
  return 0;
}

#define DETAILROWS 9

static int
iface_details(struct ncplane *hw, const interface *i, int rows){
  const int col = START_COL;
  int scrcols, scrrows;
  const int row = 1;
  int z;
  ncplane_set_fg_rgb(hw, 0xd0d0d0);
  ncplane_set_styles(hw, NCSTYLE_BOLD);
  ncplane_dim_yx(hw, &scrrows, &scrcols);
  assert(scrrows); // FIXME
  if((z = rows) >= DETAILROWS){
    z = DETAILROWS - 1;
  }
  switch(z){ // Intentional fallthroughs all the way to 0
  case (DETAILROWS - 1):{
    ncplane_printf_yx(hw, row + z, col, "drops: "U64FMT" truncs: "U64FMT" (%ju recov)%-*s",
                      i->drops, i->truncated, i->truncated_recovered,
                      scrcols - 2 - 72, "");
    --z;
  } /* intentional fallthrough */
  case 7:{
    ncplane_printf_yx(hw, row + z, col, "mform: "U64FMT" noprot: "U64FMT,
                      i->malformed, i->noprotocol);
    --z;
  } /* intentional fallthrough */
  case 6:{
    ncplane_printf_yx(hw, row + z, col, "Rbyte: "U64FMT" frames: "U64FMT,
                      i->bytes, i->frames);
    --z;
  } /* intentional fallthrough */
  case 5:{
    char b[PREFIXSTRLEN];
    char fb[PREFIXSTRLEN];
    char buf[U64STRLEN];
    ncplane_printf_yx(hw, row + z, col, "RXfd: %-4d flen: %-6u fnum: %-4s blen: %-5s bnum: %-5u rxr: %5sB",
                      i->rfd, i->rtpr.tp_frame_size,
                      bprefix(i->rtpr.tp_frame_nr, 1, fb,  1),
                      bprefix(i->rtpr.tp_block_size, 1, buf, 1),
                      i->rtpr.tp_block_nr, bprefix(i->rs, 1, b, 1));
    --z;
  } /* intentional fallthrough */
  case 4:{
    ncplane_printf_yx(hw, row + z, col, "Tbyte: "U64FMT" frames: "U64FMT" aborts: %llu",
                      i->txbytes, i->txframes, (long long unsigned)i->txaborts);
    --z;
  } /* intentional fallthrough */
  case 3:{
    char b[PREFIXSTRLEN];
    char fb[PREFIXSTRLEN];
    char buf[U64STRLEN];

    ncplane_printf_yx(hw, row + z, col, "TXfd: %-4d flen: %-6u fnum: %-4s blen: %-5s bnum: %-5u txr: %5sB",
                      i->fd, i->ttpr.tp_frame_size,
                      bprefix(i->ttpr.tp_frame_nr, 1, fb,  1),
                      bprefix(i->ttpr.tp_block_size, 1, buf, 1),
                      i->ttpr.tp_block_nr,
                      bprefix(i->ts, 1, b, 1));
    --z;
  } /* intentional fallthrough */
  case 2:{
    offload_details(hw, i, row + z, col, "TSO", TCP_SEG_OFFLOAD);
    offload_details(hw, i, row + z, col + 5, "S/G", ETH_SCATTER_GATHER);
    offload_details(hw, i, row + z, col + 10, "UFO", UDP_FRAG_OFFLOAD);
    offload_details(hw, i, row + z, col + 15, "GSO", GEN_SEG_OFFLOAD);
    offload_details(hw, i, row + z, col + 20, "GRO", GENRX_OFFLOAD);
    offload_details(hw, i, row + z, col + 25, "LRO", LARGERX_OFFLOAD);
    offload_details(hw, i, row + z, col + 30, "TCsm", TX_CSUM_OFFLOAD);
    offload_details(hw, i, row + z, col + 36, "RCsm", RX_CSUM_OFFLOAD);
    offload_details(hw, i, row + z, col + 42, "TVln", TXVLAN_OFFLOAD);
    offload_details(hw, i, row + z, col + 48, "RVln", RXVLAN_OFFLOAD);
    ncplane_printf_yx(hw, row + z, col + 53, " MTU: %-6d", i->mtu);
    --z;
  } /* intentional fallthrough */
  case 1:{
    ncplane_printf_yx(hw, row + z, col, "%-*ls", scrcols - 2, i->topinfo.devname ?
                      i->topinfo.devname : L"Unknown device");
    --z;
  } /* intentional fallthrough */
  case 0:{
    if(i->addr){
      char mac[i->addrlen * 3];

      l2ntop(i->addr, i->addrlen, mac);
      ncplane_printf_yx(hw, row + z, col, "%-16s %-*s", i->name, scrcols - (START_COL * 4 + IFNAMSIZ + 1), mac);
    }else{
      ncplane_printf_yx(hw, row + z, col, "%-16s %-*s", i->name, scrcols - (START_COL * 4 + IFNAMSIZ + 1), "");
    }
    --z;
    break;
  }default:{
    return -1;
  } }
  return 0;
}

// Pass current number of columns
int setup_statusbar(int cols){
  if(cols < 0){
    return -1;
  }
  if(statuschars <= cols){
    const size_t s = cols + 1;
    char *sm;

    if((sm = realloc(statusmsg, s * sizeof(*sm))) == NULL){
      return -1;
    }
    statuschars = s;
    if(statusmsg == NULL){
      /*time_t t = time(NULL);
      struct tm tm;*/

      // FIXME
      sm[0] = '\0';
      /*
      if(localtime_r(&t, &tm)){
        wcsftime(sm, s, L"launched at %T. 'h' toggles help.", &tm);
      }else{
        sm[0] = '\0';
      }*/
    }
    statusmsg = sm;
  }
  return 0;
}

void toggle_promisc_locked(struct ncplane *w){
  const interface *i = get_current_iface();

  if(i){
    if(interface_promisc_p(i)){
      wstatus_locked(w, "Disabling promiscuity on %s", i->name);
      disable_promiscuity(i);
    }else{
      wstatus_locked(w, "Enabling promiscuity on %s", i->name);
      enable_promiscuity(i);
    }
  }
}

void sniff_interface_locked(struct ncplane *w){
  const interface *i = get_current_iface();

  if(i){
    if(!interface_sniffing_p(i)){
      if(!interface_up_p(i)){
        wstatus_locked(w, "Bringing up %s...", i->name);
        current_iface->devaction = -1;
        up_interface(i);
      }
    }else{
      // FIXME send request to stop sniffing
    }
  }
}

void down_interface_locked(struct ncplane *w){
  const interface *i = get_current_iface();

  if(i){
    if(interface_up_p(i)){
      wstatus_locked(w, "Bringing down %s...", i->name);
      current_iface->devaction = 1;
      down_interface(i);
    }
  }
}

void hide_panel_locked(struct panel_state *ps){
  if(ps){
    ncplane_destroy(ps->n);
    ps->n = NULL;
  }
}

int packet_cb_locked(const interface *i, omphalos_packet *op, struct panel_state *ps){
  iface_state *is = op->i->opaque;
  struct timeval tdiff;
  unsigned long udiff;

  if(!is){
    return 0;
  }
  timersub(&op->tv, &is->lastprinted, &tdiff);
  udiff = timerusec(&tdiff);
  if(udiff < 100000){ // At most one update every 1/10s
    return 0;
  }
  is->lastprinted = op->tv;
  if(is->tab == ncreel_focused(reel) && ps->n){
    iface_details(ps->n, i, ps->ysize);
  }
  ncreel_redraw(reel);
  return 1;
}

static int
redraw_iface(struct nctablet *tablet, bool drawfromtop){
  const iface_state *is = nctablet_userptr(tablet);
  int rows, cols;
  ncplane_dim_yx(nctablet_plane(tablet), &rows, &cols);
  print_iface_hosts(is->iface, is, nctablet_plane(tablet),
                    rows, cols, drawfromtop, is == current_iface);
  if(interface_up_p(is->iface)){
    print_iface_state(is->iface, is, nctablet_plane(tablet), rows, cols,
                      is == current_iface);
  }
  int lines = lines_for_interface(is);
  iface_box(is->iface, is, nctablet_plane(tablet), is == current_iface, lines);
  return lines;
}

void *interface_cb_locked(struct ncreel *nreel, interface *i, iface_state *ret,
                          struct panel_state *ps){
  if(ret == NULL){
    if( (ret = create_interface_state(i)) ){
      add_channel_support(ret);
      ret->tab = ncreel_add(nreel, NULL, NULL, redraw_iface, ret);
      if(ret->tab == NULL){
        free_iface_state(ret);
        return NULL;
      }
      // calls draw_main_window(), updating iface count
      wstatus_locked(notcurses_stdplane(NC), "Set up new interface %s", i->name);
    }else{
      wstatus_locked(notcurses_stdplane(NC), "Error for new interface %s", i->name);
      return NULL;
    }
  }
  if(ret->tab == ncreel_focused(nreel) && ps->n){
    iface_details(ps->n, i, ps->ysize);
  }
  ncreel_redraw(nreel);
  if(interface_up_p(i)){
    if(ret->devaction < 0){
      wstatus_locked(notcurses_stdplane(NC), "%s", "");
      ret->devaction = 0;
    }
  }else if(ret->devaction > 0){
    wstatus_locked(notcurses_stdplane(NC), "%s", "");
    ret->devaction = 0;
  }
  screen_update();
  return ret;
}

void interface_removed_locked(struct ncreel *nreel, iface_state *is,
                              struct panel_state **ps){
  if(!is){
    return;
  }
  ncreel_del(nreel, is->tab);
  del_channel_support(is);
  free_iface_state(is); // clears l2/l3 nodes
  free(is);
  draw_main_window(notcurses_stdplane(NC)); // Update the device count
  (void)ps; // FIXME do what with it?
}

struct l2obj *neighbor_callback_locked(const interface *i, struct l2host *l2){
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
    if((ret = add_l2_to_iface(i, is, l2)) == NULL){
      return NULL;
    }
  }
  return ret;
}

struct l4obj *service_callback_locked(const struct interface *i, struct l2host *l2,
                                      struct l3host *l3, struct l4srv *l4){
  struct l2obj *l2o;
  struct l3obj *l3o;
  struct l4obj *ret;
  iface_state *is;

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
    unsigned emph = 0;

    // Want to emphasize "Router"
    if(l4_getproto(l4) == IPPROTO_IP){
      if(wcscmp(l4srvstr(l4), L"LLTD")){
        emph = 1;
      }
    }
    if((ret = add_service_to_iface(is, l2o, l3o, l4, emph)) == NULL){
      return NULL;
    }
  }
  return ret;
}

struct l3obj *host_callback_locked(const interface *i, struct l2host *l2,
                                   struct l3host *l3){
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
    if((ret = add_l3_to_iface(is, l2o, l3)) == NULL){
      return NULL;
    }
  }
  return ret;
}

// to be called only while ncurses lock is held
int draw_main_window(struct ncplane *w){
  int rows, cols;
  ncplane_dim_yx(w, &rows, &cols);
  if(setup_statusbar(cols)){
    goto err;
  }
  ncplane_styles_on(w, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(w, FOOTER_COLOR);
  // addstr() doesn't interpret format strings, so this is safe. It will
  // fail, however, if the string can't fit on the window, which will for
  // instance happen if there's an embedded newline.
  ncplane_putstr_yx(w, rows - 1, 0, statusmsg);
  ncplane_styles_off(w, NCSTYLE_BOLD);
  return 0;

err:
  return -1;
}

static void
reset_interface_stats(struct ncplane *n, const interface *i __attribute__ ((unused))){
  unimplemented(n);
}

static void
resolve_interface(struct ncplane *n){
  unimplemented(n);
}

void resolve_selection(struct ncplane *w){
  struct iface_state *is;
  if( (is = current_iface) ){
    // FIXME check for host selection...
    resolve_interface(w);
  }else{
    wstatus_locked(w, "There is no active selection");
  }
}

void reset_current_interface_stats(struct ncplane *w){
  const interface *i;

  if( (i = get_current_iface()) ){
    reset_interface_stats(w, i);
  }else{
    wstatus_locked(w, "There is no active selection");
  }
}

int expand_iface_locked(void){
  int old, oldrows;
  iface_state *is;

  if((is = current_iface) == NULL){
    return 0;
  }
  if(is->expansion == EXPANSION_MAX){
    return 0;
  }
  ++is->expansion;
  old = current_iface->selline;
  const struct ncplane *n = nctablet_plane(current_iface->tab);
  oldrows = ncplane_dim_y(n);
  recompute_selection(is, old, oldrows, ncplane_dim_y(n));
  return 0;
}

int collapse_iface_locked(void){
  int old, oldrows;
  iface_state *is;

  if((is = current_iface) == NULL){
    return 0;
  }
  if(is->expansion == 0){
    return 0;
  }
  --is->expansion;
  old = current_iface->selline;
  const struct ncplane *n = nctablet_plane(current_iface->tab);
  oldrows = ncplane_dim_y(n);
  recompute_selection(is, old, oldrows, ncplane_dim_y(n));
  return 0;
}

// Positive delta moves down, negative delta moves up, except for l2 == NULL
// where we always move to -1 (and delta is ignored).
static int
select_interface_node(struct iface_state *is, struct l2obj *l2, int delta){
  assert(l2 != is->selected);
  if((is->selected = l2) == NULL){
    is->selline = -1;
  }else{
    is->selline += delta;
  }
  return 0;
}

int select_iface_locked(void){
  struct iface_state *is;

  if((is = current_iface) == NULL){
    return -1;
  }
  if(is->selected){
    return 0;
  }
  if(is->l2objs == NULL){
    return -1;
  }
  assert(is->selline == -1);
  return select_interface_node(is, is->l2objs, 2);
}

int deselect_iface_locked(void){
  struct iface_state *is;

  if((is = current_iface) == NULL){
    return 0;
  }
  if(is->selected == NULL){
    return 0;
  }
  return select_interface_node(is, NULL, 0);
}

int display_details_locked(struct ncplane *mainw, struct panel_state *ps){
  memset(ps, 0, sizeof(*ps));
  if(new_display_panel(mainw, ps, DETAILROWS, 76, L"press 'v' to dismiss details")){
    goto err;
  }
  if(current_iface){
    if(iface_details(ps->n, current_iface->iface, ps->ysize)){
      goto err;
    }
  }
  return 0;

err:
  ncplane_destroy(ps->n);
  memset(ps, 0, sizeof(*ps));
  return -1;
}

// When this text is being displayed, the help window is the active ndow.
// Thus we refer to other ndow commands as "viewing", while 'h' here is
// described as "toggling". When other ndows come up, they list their
// own command as "toggling." We want to avoid having to scroll the help
// synopsis, so keep it under 22 lines (25 lines on an ANSI standard terminal,
// minus two for the top/bottom screen border, minus one for mandatory
// ndow top padding).
static const wchar_t *helps[] = {
  /*L"'n': network configuration",
  L"       configure addresses, routes, bridges, and wireless",
  L"'J': hijack configuration",
  L"       configure fake APs, rogue DHCP/DNS, and ARP MitM",
  L"'D': defense configuration",
  L"       define authoritative configurations to enforce",
  L"'S': secrets database",
  L"       export pilfered passwords, cookies, and identifying data",
  L"'c': crypto configuration",
  L"       configure algorithm stepdown, WEP/WPA cracking, SSL MitM", */
  //L"'m': change device MAC        'u': change device MTU",
  L"'q': quit                     ctrl+'L': redraw the screen",
  L"'⇆Tab' move between displays  'P': toggle subdisplay pinning",
  L"'e': view environment details 'h': toggle this help display",
  L"'v': view interface details   'n': view network stack details",
  L"'w': view wireless info       'b': view bridging info",
  L"'a': attack configuration     'l': view recent diagnostics",
  L"'⏎Enter': browse interface    'BkSpc': leave interface browser",
  L"'k'/'↑': previous selection   'j'/'↓': next selection",
  L"PageUp: previous page         PageDown: next page",
  L"'↖Home': first selection      '↘End': last selection",
  L"'-'/'←': collapse selection   '+'/'→': expand selection",
  L"'r': reset selection's stats  'D': reresolve selection",
  L"'d': bring down device        'p': toggle promiscuity",
  L"'s': toggle sniffing, bringing up interface if down",
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
helpstrs(struct ncplane *hw, int row, int rows){
  const wchar_t *hs;
  int z;

  ncplane_set_styles(hw, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(hw, 0xd0d0d0);
  for(z = 0 ; (hs = helps[z]) && z < rows ; ++z){
    ncplane_putwstr_yx(hw, row + z, 1, hs);
  }
  return 0;
}

static const int DIAGROWS = 8; // FIXME

int update_diags_locked(struct panel_state *ps){
  logent l[DIAGROWS];
  int y, x, r;

  ncplane_set_styles(ps->n, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(ps->n, 0xd0d0d0);
  ncplane_dim_yx(ps->n, &y, &x);
  assert(x > 26 + START_COL * 2);
  if(get_logs(y - 1, l)){
    return -1;
  }
  for(r = 1 ; r < y - 1 ; ++r){
    char tbuf[26]; // see ctime_r(3)

    if(l[r - 1].msg == NULL){
      break;
    }
    ctime_r(&l[r - 1].when, tbuf);
    tbuf[strlen(tbuf) - 1] = ' '; // kill newline
    ncplane_printf_yx(ps->n, y - r - 1, START_COL, "%-*.*s%-*.*s", 25, 25, tbuf,
                      x - 25 - START_COL * 2, x - 25 - START_COL * 2,
                      l[r - 1].msg);
    free(l[r - 1].msg);
  }
  return 0;
}

int display_diags_locked(struct ncplane *mainw, struct panel_state *ps){
  int x, y;
  ncplane_dim_yx(mainw, &y, &x);
  assert(y);
  memset(ps, 0, sizeof(*ps));
  if(new_display_panel(mainw, ps, DIAGROWS, x - START_COL * 4,
                       L"press 'l' to dismiss diagnostics")){
    goto err;
  }
  if(update_diags_locked(ps)){
    goto err;
  }
  return 0;

err:
  ncplane_destroy(ps->n);
  memset(ps, 0, sizeof(*ps));
  return -1;
}

int display_help_locked(struct ncplane *mainw, struct panel_state *ps){
  static const int helprows = sizeof(helps) / sizeof(*helps) - 1; // NULL != row
  const int helpcols = max_helpstr_len(helps) + 4; // spacing + borders

  memset(ps, 0, sizeof(*ps));
  if(new_display_panel(mainw, ps, helprows, helpcols, L"press 'h' to dismiss help")){
    goto err;
  }
  if(helpstrs(ps->n, 1, ps->ysize)){
    goto err;
  }
  return 0;

err:
  ncplane_destroy(ps->n);
  memset(ps, 0, sizeof(*ps));
  return -1;
}

void use_next_node_locked(void){
  struct iface_state *is;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if(is->selected == NULL || l2obj_next(is->selected) == NULL){
    return;
  }
  delta = l2obj_lines(is->selected);
  const struct ncplane *n = nctablet_plane(is->tab);
  if(is->selline + delta + l2obj_lines(l2obj_next(is->selected)) >= ncplane_dim_y(n) - 1){
    delta = (ncplane_dim_y(n) - 1 - l2obj_lines(l2obj_next(is->selected)))
       - is->selline;
  }
  select_interface_node(is, l2obj_next(is->selected), delta);
}

void use_prev_node_locked(void){
  struct iface_state *is;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if(is->selected == NULL || l2obj_prev(is->selected) == NULL){
    return;
  }
  delta = -l2obj_lines(l2obj_prev(is->selected));
  if(is->selline + delta <= !!interface_up_p(is->iface)){
    delta = !!interface_up_p(is->iface) - is->selline;
  }
  select_interface_node(is, l2obj_prev(is->selected), delta);
}

void use_next_nodepage_locked(void){
  struct iface_state *is;
  struct l2obj *l2;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if((l2 = is->selected) == NULL || l2obj_next(l2) == NULL){
    return;
  }
  delta = 0;
  const struct ncplane *n = nctablet_plane(is->tab);
  while(l2obj_next(l2) && delta <= ncplane_dim_y(n)){
    delta += l2obj_lines(l2);
    l2 = l2obj_next(l2);
  }
  if(delta == 0){
    return;
  }
  if(is->selline + delta + l2obj_lines(l2) >= ncplane_dim_y(n) - 1){
    delta = (ncplane_dim_y(n) - 2 - l2obj_lines(l2)) - is->selline;
  }
  select_interface_node(is, l2, delta);
}

void use_prev_nodepage_locked(void){
  struct iface_state *is;
  struct l2obj *l2;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if((l2 = is->selected) == NULL || l2obj_prev(l2) == NULL){
    return;
  }
  delta = 0;
  do{
    l2 = l2obj_prev(l2);
    delta -= l2obj_lines(l2);
  }while(l2obj_prev(l2) && delta >= -ncplane_dim_y(nctablet_plane(is->tab)));
  if(is->selline + delta <= !!interface_up_p(is->iface)){
    delta = !!interface_up_p(is->iface) - is->selline;
  }
  select_interface_node(is, l2, delta);
}

void use_first_node_locked(void){
  struct iface_state *is;
  struct l2obj *l2;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if((l2 = is->selected) == NULL || l2obj_prev(l2) == NULL){
    return;
  }
  delta = 0;
  do{
    l2 = l2obj_prev(l2);
    delta -= l2obj_lines(l2);
  }while(l2obj_prev(l2));
  if(is->selline + delta <= !!interface_up_p(is->iface)){
    delta = !!interface_up_p(is->iface) - is->selline;
  }
  select_interface_node(is, l2, delta);
}

void use_last_node_locked(void){
  struct iface_state *is;
  struct l2obj *l2;
  int delta;

  if((is = current_iface) == NULL){
    return;
  }
  if((l2 = is->selected) == NULL || l2obj_next(l2) == NULL){
    return;
  }
  delta = 0;
  while(l2obj_next(l2)){
    delta += l2obj_lines(l2);
    l2 = l2obj_next(l2);
  }
  if(delta == 0){
    return;
  }
  const struct ncplane *n = nctablet_plane(is->tab);
  if(is->selline + delta + l2obj_lines(l2) >= ncplane_dim_y(n) - 1){
    delta = (ncplane_dim_y(n) - 2 - l2obj_lines(l2)) - is->selline;
  }
  select_interface_node(is, l2, delta);
}

// Whether we've entered an interface, and are browsing selected nodes within.
int selecting(void){
  if(!current_iface){
    return 0;
  }
  if(!current_iface->selected){
    return 0;
  }
  return 1;
}
