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
#include <ui/notcurses/iface.h>
#include <omphalos/interface.h>
#include <notcurses/notcurses.h>

typedef struct l4obj {
  struct l4obj *next;
  struct l4srv *l4;
  unsigned emph;      // Emphasize the service in display?
} l4obj;

typedef struct l3obj {
  struct l3obj *next;
  struct l3host *l3;
  struct l4obj *l4objs;
  struct l2obj *l2;    // FIXME coverup of real failure
} l3obj;

typedef struct l2obj {
  struct l2obj *next, *prev;
  struct l2host *l2;
  unsigned lnes;      // number of lines node would take up
  int cat;      // cached result of l2categorize()
  struct l3obj *l3objs;
} l2obj;

#define UBORDER_FG 0x00d7ff
#define UBORDER_BG 0x0
#define DBORDER_FG 0x848789
#define DBORDER_BG 0x0

static int
draw_right_vline(const interface *i, int active, struct ncplane *n){
  //assert(i && w && (active || !active));
  ncplane_set_styles(n, active ? NCSTYLE_REVERSE : NCSTYLE_BOLD);
  ncplane_set_fg_rgb(n, interface_up_p(i) ? UBORDER_FG : DBORDER_FG);
  ncplane_set_bg_rgb(n, interface_up_p(i) ? UBORDER_BG : DBORDER_BG);
  if(ncplane_putwc(n,  L'│') <= 0){
    return -1;
  }
  return 0;
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

  if((tstr = lookup_arptype(i->arptype, NULL, NULL)) == NULL){
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
    ret->scrline = -1;
    ret->selected = NULL;
    ret->selline = -1;
    ret->tab = NULL;
  }
  return ret;
}

static l2obj *
get_l2obj(const interface *i, const iface_state *is, struct l2host *l2){
  l2obj *l;

  if( (l = malloc(sizeof(*l))) ){
    l->lnes = is->expansion > EXPANSION_NONE;
    l->cat = l2categorize(i, l2);
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
get_l4obj(struct l4srv *srv, unsigned emph){
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
node_lines(int e, const l2obj *l){
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
l2catcmp(int c0, int c1){
  // not a surjection! some values are shared.
  static const int vals[__RTN_MAX] = {
    0,      // RTN_UNSPEC
    __RTN_MAX - 1,    // RTN_UNICAST
    __RTN_MAX,    // RTN_LOCAL
    __RTN_MAX - 5,    // RTN_BROADCAST
    __RTN_MAX - 4,    // RTN_ANYCAST
    __RTN_MAX - 3,    // RTN_MULTICAST
    __RTN_MAX - 2,    // RTN_BLACKHOLE
    __RTN_MAX - 2,    // RTN_UNREACHABLE
    __RTN_MAX - 2,    // RTN_PROHIBIT
          // 0 the rest of the way...
  };
  return vals[c0] - vals[c1];
}

l2obj *add_l2_to_iface(const interface *i, iface_state *is, struct l2host *l2h){
  assert(i);
  assert(is);
  l2obj *l2;
  if( (l2 = get_l2obj(i, is, l2h)) ){
    if(is->nodes == 0 && is->vnodes == 0){
      is->l2objs = l2;
      l2->next = l2->prev = NULL;
    }else{
      l2obj **prev, *prec;

      for(prec = NULL, prev = &is->l2objs ; *prev ; prec = *prev, prev = &(*prev)->next){
        // we want the inverse of l2catcmp()'s priorities
        if(l2catcmp(l2->cat, (*prev)->cat) > 0){
          break;
        }else if(l2catcmp(l2->cat, (*prev)->cat) == 0){
          if(l2hostcmp(l2->l2, (*prev)->l2, i->addrlen) <= 0){
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

l3obj *add_l3_to_iface(iface_state *is, l2obj *l2, struct l3host *l3h){
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

l4obj *add_service_to_iface(iface_state *is, struct l2obj *l2, struct l3obj *l3,
                            struct l4srv *srv, unsigned emph){
  l4obj *l4;

  // FIXME we ought be able to use this assert (or preferably get rid of
  // l3->l2 entirely), but it's being triggered (see bug 415). until we
  // get that resolved, we use the bogon check....
  // assert(l3->l2 == l2);
  if(l3->l2 != l2){
    return NULL;
  }
  if( (l4 = get_l4obj(srv, emph)) ){
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
          if(wcscmp(l4srvstr(srv), l4srvstr(c)) < 0){
            break;
          }
        }
      }
    }while( *(prev = &(*prev)->next) );
    l4->next = *prev;
    *prev = l4;
  }
  node_lines(is->expansion, l3->l2);
  node_lines(is->expansion, l2);
  return l4;
}

static void
print_host_services(struct ncplane *nc, const interface *i, const l3obj *l,
                    int *line, int rows, int cols, wchar_t selectchar,
                    uint32_t rgb, unsigned styles, int minline, int active){
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
  n = 0;
  for(l4 = l->l4objs ; l4 ; l4 = l4->next){
    ncplane_set_fg_rgb(nc, rgb);
    ncplane_set_styles(nc, styles | (l4->emph ? NCSTYLE_BOLD : 0));
    srv = l4srvstr(l4->l4);
    if(n){
      if((unsigned)cols < 1 + wcslen(srv)){ // one for space
        break;
      }
      n += 1 + wcslen(srv);
      ncplane_printf(nc, " %ls", srv);
    }else{
      if((unsigned)cols < 2 + 5 + wcslen(srv)){ // two for borders
        break;
      }
      n += 5 + wcslen(srv);
      ncplane_printf_yx(nc, *line, 0, "%lc    %ls", selectchar, srv);
      ++*line;
    }
  }
  int cy, cx;
  ncplane_cursor_yx(nc, &cy, &cx);
  if(n && n < cols - 1){
    ncplane_cursor_move_yx(nc, cy, cols - 1);
    draw_right_vline(i, active, nc);
  }
}

#define UCAST_COLOR 0x00afff
#define MCAST_COLOR 0x0087ff
#define LCAST_COLOR 0x00ff87
#define BCAST_COLOR 0x8700ff
#define DHEADING_COLOR 0xd0d0d0
#define UHEADING_COLOR 0xff8700
#define BULKTEXT_COLOR 0xd0d0d0
#define BULKTEXT_ALTROW_COLOR 0xffff5f
#define IFACE_COLOR 0xd0d0d0
#define LSELECTED_COLOR 0x87ffd7
#define USELECTED_COLOR 0x0087d7
#define MSELECTED_COLOR 0x00afff
#define BSELECTED_COLOR 0xff87af
#define LCAST_L3_COLOR 0x5fd7af
#define UCAST_L3_COLOR 0xd787ff
#define MCAST_L3_COLOR 0x0087d7
#define BCAST_L3_COLOR 0x5f87d7
#define LCAST_RES_COLOR 0x5fa55f
#define UCAST_RES_COLOR 0x5fd7ff
#define MCAST_RES_COLOR 0xafd7ff
#define BCAST_RES_COLOR 0xaf87d7
#define LCAST_ALTROW_COLOR 0xafff5f
#define UCAST_ALTROW_COLOR 0xafd7ff
#define MCAST_ALTROW_COLOR 0xd7ffff
#define BCAST_ALTROW_COLOR 0xff00d7
#define LCAST_ALTROW_L3_COLOR 0xd7fff8
#define UCAST_ALTROW_L3_COLOR 0xd7d7ff
#define MCAST_ALTROW_L3_COLOR 0xd7afff
#define BCAST_ALTROW_L3_COLOR 0xff00af
#define LCAST_ALTROW_RES_COLOR 0xd7ff00
#define UCAST_ALTROW_RES_COLOR 0xd7afd7
#define MCAST_ALTROW_RES_COLOR 0x5f5fff
#define BCAST_ALTROW_RES_COLOR 0x5f005f

// line: line on which this node starts, within the ncplane w of {rows x cols}
static void
print_iface_host(const interface *i, const iface_state *is, struct ncplane *w,
                 const l2obj *l, int line, int rows, int cols, int selected,
                 int minline, int active){
  uint16_t sty = NCSTYLE_NONE;
  uint16_t asty = NCSTYLE_NONE;
  uint16_t al3sty = NCSTYLE_NONE;
  uint16_t arsty = NCSTYLE_NONE;
  uint16_t l3sty = NCSTYLE_NONE;
  uint16_t rsty = NCSTYLE_NONE;
  uint32_t rgb = 0xffffff;
  uint32_t argb = 0xffffff;
  uint32_t al3rgb = 0xffffff;
  uint32_t arrgb = 0xffffff;
  uint32_t l3rgb = 0xffffff;
  uint32_t rrgb = 0xffffff;
  uint32_t srgb = 0xffffff;
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
  if(line >= rows){
    return;
  }
  switch(l->cat){
    case RTN_UNICAST:
      rgb = UCAST_COLOR;
      argb = UCAST_ALTROW_COLOR;
      l3rgb = UCAST_L3_COLOR;
      rrgb = UCAST_RES_COLOR;
      srgb = USELECTED_COLOR;
      arrgb = UCAST_ALTROW_RES_COLOR;
      al3rgb = UCAST_ALTROW_L3_COLOR;
      devname = get_devname(l->l2);
      legend = 'U';
      break;
    case RTN_LOCAL:
      rgb = LCAST_COLOR;
      argb = LCAST_ALTROW_COLOR;
      sty = NCSTYLE_BOLD;
      asty = NCSTYLE_BOLD;
      l3rgb = LCAST_L3_COLOR;
      l3sty = NCSTYLE_BOLD;
      rrgb = LCAST_RES_COLOR;
      rsty = NCSTYLE_BOLD;
      srgb = LSELECTED_COLOR;
      arrgb = LCAST_ALTROW_RES_COLOR;
      arsty = NCSTYLE_BOLD;
      al3rgb = LCAST_ALTROW_L3_COLOR;
      al3sty = NCSTYLE_BOLD;
      if(interface_virtual_p(i) ||
        (devname = get_devname(l->l2)) == NULL){
        devname = i->topinfo.devname;
      }
      legend = 'L';
      break;
    case RTN_MULTICAST:
      rgb = MCAST_COLOR;
      argb = MCAST_ALTROW_COLOR;
      l3rgb = MCAST_L3_COLOR;
      rrgb = MCAST_RES_COLOR;
      srgb = MSELECTED_COLOR;
      arrgb = MCAST_ALTROW_RES_COLOR;
      al3rgb = MCAST_ALTROW_L3_COLOR;
      devname = get_devname(l->l2);
      legend = 'M';
      break;
    case RTN_BROADCAST:
      rgb = BCAST_COLOR;
      argb = BCAST_ALTROW_COLOR;
      l3rgb = BCAST_L3_COLOR;
      rrgb = BCAST_RES_COLOR;
      srgb = BSELECTED_COLOR;
      arrgb = BCAST_ALTROW_RES_COLOR;
      al3rgb = BCAST_ALTROW_L3_COLOR;
      devname = get_devname(l->l2);
      legend = 'B';
      break;
    default:
      assert(0 && "Unknown l2 category");
      return;
  }
  if(!interface_up_p(i)){
    srgb =  BULKTEXT_ALTROW_COLOR;
    sty = sty & NCSTYLE_BOLD;
    rgb = BULKTEXT_COLOR;
    asty = arsty & NCSTYLE_BOLD;
    argb =  BULKTEXT_ALTROW_COLOR;
    l3sty = l3sty & NCSTYLE_BOLD;
    l3rgb = BULKTEXT_COLOR;
    rsty = rsty & NCSTYLE_BOLD;
    rrgb = BULKTEXT_COLOR;
    al3sty = al3sty & NCSTYLE_BOLD;
    al3rgb =  BULKTEXT_ALTROW_COLOR;
    arsty = rsty & NCSTYLE_BOLD;
    arrgb =  BULKTEXT_ALTROW_COLOR;
  }
  if(selected){
    argb = rgb = srgb;
    al3rgb = l3rgb = srgb;
    arrgb = rrgb = srgb;
    selectchar = l->l3objs && is->expansion >= EXPANSION_HOSTS ? L'┌' : L'[';
  }else{
    selectchar = L' ';
  }
  if(!(line % 2)){
    ncplane_set_fg_rgb(w, rgb);
    ncplane_set_styles(w, sty);
  }else{
    ncplane_set_fg_rgb(w, argb);
    ncplane_set_styles(w, asty);
  }
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
      ncplane_printf_yx(w, line, 0, "%lc%c %s %-*.*ls",
        selectchar, legend, hw, len, len, devname);
    }else{
      len = cols - PREFIXCOLUMNS * 2 - 5;
      if(!interface_up_p(i)){
        len += PREFIXCOLUMNS * 2 + 1;
      }
      ncplane_printf_yx(w, line, 0, "%lc%c %-*.*s",
        selectchar, legend, len, len, hw);
    }
    if(interface_up_p(i)){
      char dbuf[PREFIXCOLUMNS + 1];
      if(get_srcpkts(l->l2) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
        qprefix(get_dstpkts(l->l2), 1, dbuf,  1);
        ncplane_printf(w, "%-*.*s%*s", PREFIXCOLUMNS + 1, PREFIXCOLUMNS + 1, "", PREFIXFMT(dbuf));
      }else{
        char sbuf[PREFIXCOLUMNS + 1];
        qprefix(get_srcpkts(l->l2), 1, sbuf,  1);
        qprefix(get_dstpkts(l->l2), 1, dbuf,  1);
        ncplane_printf(w, "%*s %*s",  PREFIXFMT(sbuf), PREFIXFMT(dbuf));
      }
    }
    draw_right_vline(i, active, w);
  }
  ++line;
  if(is->expansion >= EXPANSION_HOSTS){
    for(l3 = l->l3objs ; l3 ; l3 = l3->next){
      char nw[INET6_ADDRSTRLEN + 1]; // FIXME
      const wchar_t *name;

      if(selectchar != L' '){
        if(l3->next || (l3->l4objs && is->expansion >= EXPANSION_SERVICES)){
          selectchar = L'│';
        }else{
          selectchar = L'└';
        }
      }
      if(line >= rows){
        break;
      }
      if(line >= minline){
        int len, wlen;

        if(!(line % 2)){
          ncplane_set_fg_rgb(w, l3rgb);
          ncplane_set_styles(w, l3sty);
        }else{
          ncplane_set_fg_rgb(w, al3rgb);
          ncplane_set_styles(w, al3sty);
        }
        l3ntop(l3->l3, nw, sizeof(nw));
        if((name = get_l3name(l3->l3)) == NULL){
          name = L"";
        }
        ncplane_printf_yx(w, line, 0, "%lc   %s ", selectchar, nw);
        if(!(line % 2)){
          ncplane_set_fg_rgb(w, rrgb);
          ncplane_set_styles(w, rsty);
        }else{
          ncplane_set_fg_rgb(w, arrgb);
          ncplane_set_styles(w, arsty);
        }
        len = cols - PREFIXCOLUMNS * 2 - 7 - strlen(nw);
        wlen = len - wcswidth(name, wcslen(name));
        if(wlen < 0){
          wlen = 0;
        }
        ncplane_printf(w, "%.*ls%*.*s", len, name, wlen, wlen, "");
        if(!(line % 2)){
          ncplane_set_fg_rgb(w, l3rgb);
          ncplane_set_styles(w, l3sty);
        }else{
          ncplane_set_fg_rgb(w, al3rgb);
          ncplane_set_styles(w, al3sty);
        }
        {
          char sbuf[PREFIXSTRLEN + 1];
          char dbuf[PREFIXSTRLEN + 1];
          if(l3_get_srcpkt(l3->l3) == 0 && (l->cat == RTN_MULTICAST || l->cat == RTN_BROADCAST)){
            qprefix(l3_get_dstpkt(l3->l3), 1, dbuf,  1);
            ncplane_printf(w, "%-*.*s%*s", PREFIXCOLUMNS + 1, PREFIXCOLUMNS + 1,
                "", PREFIXFMT(dbuf));
          }else{
            qprefix(l3_get_srcpkt(l3->l3), 1, sbuf,  1);
            qprefix(l3_get_dstpkt(l3->l3), 1, dbuf,  1);
            ncplane_printf(w, "%*s %*s", PREFIXFMT(sbuf), PREFIXFMT(dbuf));
          }
        }
        draw_right_vline(i, active, w);
      }
      ++line;
      if(is->expansion >= EXPANSION_SERVICES){
        if(selectchar != L' ' && !l3->next){
          selectchar = L'└';
        }
        print_host_services(w, i, l3, &line, rows, cols, selectchar,
                            !(line % 2) ? rgb : argb, !(line % 2) ? sty : asty,
                            minline, active);
      }
    }
  }
}

void print_active_iface_hosts(const interface *i, const iface_state *is,
                              struct ncplane *w, int rows, int cols){
  // If the interface is down, we don't lead with the summary line
  const int sumline = !!interface_up_p(i);
  const struct l2obj *cur;
  long line;

  if(is->expansion < EXPANSION_NODES){
    return;
  }
  // First, print the selected interface (if there is one), and those above
  cur = is->selected;
  line = is->selline + sumline;
  while(cur && line + (long)cur->lnes >= sumline){
    print_iface_host(i, is, w, cur, line, rows, cols, cur == is->selected,
                     sumline, true);
    // here we traverse, then account...
    if( (cur = cur->prev) ){
      line -= cur->lnes;
    }
  }
  line = is->selected ? (is->selline + (long)is->selected->lnes) : 1;
  line += sumline;
  cur = (is->selected ? is->selected->next : is->l2objs);
  while(cur && line < rows){
    print_iface_host(i, is, w, cur, line, rows, cols, 0, 0, true);
    // here, we account before we traverse. this is correct.
    line += cur->lnes;
    cur = cur->next;
  }
}

// rather than pivoting around the selected line, we simply dump from either
// the top or bottom, going in the other direction.
void print_inactive_iface_hosts(const interface *i, const iface_state *is,
                                struct ncplane *w, int rows, int cols,
                                bool drawfromtop){
  // If the interface is down, we don't lead with the summary line
  const int sumline = !!interface_up_p(i);
  const struct l2obj *cur;

  if(is->expansion < EXPANSION_NODES){
    return;
  }
  if((cur = is->l2objs) == NULL){
    return;
  }
  long line = 1 + sumline;
  int skiprows = 0;
  if(!drawfromtop){
    int datarows = lines_for_interface(is); // FIXME o(n)
    if(datarows > rows){
      skiprows = datarows - rows;
    }
  }
  while(cur && line + (long)cur->lnes <= rows){
    print_iface_host(i, is, w, cur, line, rows, cols, false, 0, false);
    int curlines = cur->lnes;
    if(skiprows){
      if(skiprows <= curlines){
        skiprows = 0;
        curlines -= skiprows;
      }else{
        skiprows -= curlines;
        curlines = 0;
      }
    }
    line += curlines;
    cur = cur->next;
  }
}

static int
iface_optstr(struct ncplane *n, const char *str, int hcolor, int bcolor){
  if(ncplane_set_fg_rgb(n, bcolor)){
    return -1;
  }
  if(ncplane_putchar(n, '|') < 1){
    return -1;
  }
  if(ncplane_set_fg_rgb(n, hcolor)){
    return -1;
  }
  if(ncplane_putstr(n, str) < 0){
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

// since we haven't been resized yet, we can't just use ncplane_dim_yx() to
// get placement info for the bottom line. instead, we accept 'rows' as an
// explicit parameter.
void iface_box(const interface *i, const iface_state *is, struct ncplane *n,
               int active, int rows){
  int bcolor, hcolor, cols;
  size_t buslen;
  int attrs;

  cols = ncplane_dim_x(n);
  bcolor = interface_up_p(i) ? UBORDER_FG : DBORDER_FG;
  hcolor = interface_up_p(i) ? UHEADING_COLOR : DHEADING_COLOR;
  attrs = active ? NCSTYLE_REVERSE : NCSTYLE_BOLD;
  ncplane_set_styles(n, attrs);
  ncplane_set_fg_rgb(n, bcolor);
  ncplane_cursor_move_yx(n, 0, 1);
  cell c = CELL_TRIVIAL_INITIALIZER;
  cell_load(n, &c, "─");
  cell_set_fg_rgb(&c, bcolor);
  cell_set_styles(&c, attrs);
  ncplane_hline(n, &c, cols - 3);
  ncplane_putegc_yx(n, 0, cols - 1, "╮", NULL);
  ncplane_off_styles(n, NCSTYLE_REVERSE);
  if(active){
    ncplane_on_styles(n, NCSTYLE_BOLD);
  }
  ncplane_printf_yx(n, 0, 0, "[");
  ncplane_set_fg_rgb(n, hcolor);
  if(active){
    ncplane_on_styles(n, NCSTYLE_BOLD);
  }else{
    ncplane_off_styles(n, NCSTYLE_BOLD);
  }
  ncplane_putstr(n, i->name);
  ncplane_printf(n, " (%s", is->typestr);
  if(strlen(i->drv.driver)){
    ncplane_putchar(n, ' ');
    ncplane_putstr(n, i->drv.driver);
    if(strlen(i->drv.version)){
      ncplane_printf(n, " %s", i->drv.version);
    }
    if(strlen(i->drv.fw_version)){
      ncplane_printf(n, " fw %s", i->drv.fw_version);
    }
  }
  ncplane_putchar(n, ')');
  ncplane_set_fg_rgb(n, bcolor);
  if(active){
    ncplane_on_styles(n, NCSTYLE_BOLD);
  }
  ncplane_printf(n, "]");
  ncplane_cursor_move_yx(n, 0, cols - 4);
  ncplane_on_styles(n, NCSTYLE_BOLD);
  ncplane_putstr(n, is->expansion == EXPANSION_MAX ? "[-]" :
                  is->expansion == 0 ? "[+]" : "[±]");
  // now we do the bottom
  ncplane_cursor_move_yx(n, rows - 1, 0);
  ncplane_set_fg_rgb(n, 0);
  ncplane_set_bg_rgb(n, 0);
  ncplane_putstr(n, "  ");
  ncplane_set_fg_rgb(n, bcolor);
  ncplane_on_styles(n,  attrs);
  ncplane_off_styles(n,  NCSTYLE_REVERSE);
  attrs = NCSTYLE_BOLD | (active ? NCSTYLE_REVERSE : 0);
  ncplane_set_styles(n, attrs);
  cell_set_fg_rgb(&c, bcolor);
  cell_set_styles(&c, attrs);
  ncplane_hline(n, &c, cols - 3);
  cell_release(n, &c);
  ncplane_putegc_yx(n, rows - 1, cols - 1, "╯", NULL);
  ncplane_printf_yx(n, rows - 1, 2, "[");
  ncplane_set_fg_rgb(n, hcolor);
  if(active){
    ncplane_on_styles(n, NCSTYLE_BOLD);
  }else{
    ncplane_off_styles(n, NCSTYLE_BOLD);
  }
  ncplane_printf(n, "mtu %d", i->mtu);
  if(interface_up_p(i)){
    char buf[U64STRLEN + 1];

    iface_optstr(n, "up", hcolor, bcolor);
    if(i->settings_valid == SETTINGS_VALID_ETHTOOL){
      if(!interface_carrier_p(i)){
        ncplane_putstr(n, " (no carrier)");
      }else{
        ncplane_printf(n, " (%sb %s)", qprefix(i->settings.ethtool.speed * (uint64_t)1000000lu, 1, buf,  1),
              duplexstr(i->settings.ethtool.duplex));
      }
    }else if(i->settings_valid == SETTINGS_VALID_WEXT){
      if(i->settings.wext.mode == NL80211_IFTYPE_MONITOR){
        ncplane_printf(n, " (%s", modestr(i->settings.wext.mode));
      }else if(!interface_carrier_p(i)){
        ncplane_printf(n, " (%s, no carrier", modestr(i->settings.wext.mode));
      }else{
        ncplane_printf(n, " (%sb %s", qprefix(i->settings.wext.bitrate, 1, buf,  1),
              modestr(i->settings.wext.mode));
      }
      if(i->settings.wext.freq >= MAX_WIRELESS_CHANNEL){
        ncplane_printf(n, " %sHz)", qprefix(i->settings.wext.freq, 1, buf,  1));
      }else if(i->settings.wext.freq){
        ncplane_printf(n, " ch %ju)", i->settings.wext.freq);
      }else{
        ncplane_printf(n, ")");
      }
    }else if(i->settings_valid == SETTINGS_VALID_NL80211){
      if(i->settings.nl80211.mode == NL80211_IFTYPE_MONITOR){
        ncplane_printf(n, " (%s", modestr(i->settings.nl80211.mode));
      }else if(!interface_carrier_p(i)){
        ncplane_printf(n, " (%s, no carrier", modestr(i->settings.nl80211.mode));
      }else{
        ncplane_printf(n, " (%sb %s", qprefix(i->settings.nl80211.bitrate, 1, buf,  1),
              modestr(i->settings.nl80211.mode));
      }
      if(i->settings.nl80211.freq >= MAX_WIRELESS_CHANNEL){
        ncplane_printf(n, " %sHz)", qprefix(i->settings.nl80211.freq, 1, buf,  1));
      }else if(i->settings.nl80211.freq){
        ncplane_printf(n, " ch %ju)", i->settings.nl80211.freq);
      }else{
        ncplane_printf(n, ")");
      }
    }
  }else{
    iface_optstr(n, "down", hcolor, bcolor);
    if(i->settings_valid == SETTINGS_VALID_WEXT){
      ncplane_printf(n, " (%s)", modestr(i->settings.wext.mode));
    }
  }
  if(interface_promisc_p(i)){
    iface_optstr(n, "promisc", hcolor, bcolor);
  }
  ncplane_set_fg_rgb(n, bcolor);
  if(active){
    ncplane_on_styles(n, NCSTYLE_BOLD);
  }
  ncplane_printf(n, "]");
  if( (buslen = strlen(i->drv.bus_info)) ){
    if(active){
      // FIXME Want the text to be bold -- currently unreadable
      ncplane_set_styles(n, NCSTYLE_REVERSE);
      ncplane_set_fg_rgb(n, bcolor);
    }else{
      ncplane_set_styles(n, NCSTYLE_BOLD);
      ncplane_set_fg_rgb(n, bcolor);
    }
    if(i->busname){
      buslen += strlen(i->busname) + 1;
      ncplane_printf_yx(n, rows - 1, cols - (buslen + 2), "%s:%s", i->busname, i->drv.bus_info);
    }else{
      ncplane_printf_yx(n, rows - 1, cols - (buslen + 2), "%s", i->drv.bus_info);
    }
  }
}

void print_iface_state(const interface *i, const iface_state *is, struct ncplane *w,
                       int rows, int cols, int active){
  char buf[U64STRLEN + 1], buf2[U64STRLEN + 1];
  unsigned long usecdomain;

  if(rows < 2){
    return;
  }
  ncplane_set_styles(w, NCSTYLE_BOLD);
  ncplane_set_fg_rgb(w, IFACE_COLOR);
  // FIXME broken if bps domain ever != fps domain. need unite those
  // into one FTD stat by letting it take an object...
  // FIXME this leads to a "ramp-up" period where we approach steady state
  usecdomain = i->bps.usec * i->bps.total;
  ncplane_printf_yx(w, 1, 0, "%u node%s. Last %lus: %7sb/s (%sp)",
    is->nodes, is->nodes == 1 ? "" : "s",
    usecdomain / 1000000,
    qprefix(timestat_val(&i->bps) * CHAR_BIT * 1000000 * 100 / usecdomain, 100, buf,  0),
    qprefix(timestat_val(&i->fps), 1, buf2,  1));
  ncplane_putstr_yx(w, 1, cols - PREFIXCOLUMNS * 2 - 1, "TotSrc  TotDst");
  draw_right_vline(i, active, w);
}

void free_iface_state(iface_state *is){
  l2obj *l2 = is->l2objs;

  while(l2){
    l2obj *tmp = l2->next;
    free_l2obj(l2);
    l2 = tmp;
  }
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

// Recompute ->lnes values for all nodes, and return the number of lines of
// output available before and after the current selection. If there is no
// current selection, the return value ought not be ascribed meaning. O(N) on
// the number of l2hosts, not just those visible -- unacceptable! FIXME
static void
recompute_lines(iface_state *is, int *before, int *after){
  int newsel;
  l2obj *l;

  *after = -1;
  *before = -1;
  newsel = !!interface_up_p(is->iface);
  for(l = is->l2objs ; l ; l = l->next){
    l->lnes = node_lines(is->expansion, l);
    if(l == is->selected){
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
void recompute_selection(iface_state *is, int oldsel, int oldrows, int newrows){
  int newsel, bef, aft;

  // Calculate the maximum new line -- we can't leave space at the top or
  // bottom, so we can't be after the true number of lines of output that
  // precede us, or before the true number that follow us.
  recompute_lines(is, &bef, &aft);
  if(bef < 0 || aft < 0){
    assert(!is->selected);
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
  /*wstatus_locked(stdscr, "newsel: %d bef: %d aft: %d oldsel: %d maxy: %d",
      newsel, bef, aft, oldsel, ncplane_dim_y(is->tab));
  update_panels();
  doupdate();*/
  const struct ncplane *n = nctablet_plane(is->tab);
  if(newsel + aft <= ncplane_dim_y(n) - 2 - !!interface_up_p(is->iface)){
    newsel = ncplane_dim_y(n) - aft - 2 - !!interface_up_p(is->iface);
  }
  if(newsel + (int)node_lines(is->expansion, is->selected) >= ncplane_dim_y(n) - 2){
    newsel = ncplane_dim_y(n) - 2 - node_lines(is->expansion, is->selected);
  }
  /*wstatus_locked(stdscr, "newsel: %d bef: %d aft: %d oldsel: %d maxy: %d",
      newsel, bef, aft, oldsel, ncplane_dim_y(is->tab));
  update_panels();
  doupdate();*/
  if(newsel){
    is->selline = newsel;
  }
  assert(is->selline >= 1);
  assert(is->selline < ncplane_dim_y(n) - 1 || !is->expansion);
}
