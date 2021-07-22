#include <errno.h>
#include <ctype.h>
#include <net/if.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <pthread.h>
#include <langinfo.h>
#include <sys/time.h>
#include <sys/socket.h>

// The wireless extensions headers are not so fantastic. This workaround comes
// to us courtesy of Jean II in iwlib.h. Ugh.
#ifndef __user
#define __user
#endif
#include <asm/types.h>
#include <wireless.h>

#include <sys/utsname.h>
#include <linux/version.h>
#include <linux/rtnetlink.h>
#include <omphalos/timing.h>
#include <omphalos/hwaddrs.h>
#include <gnu/libc-version.h>
#include <omphalos/ethtool.h>
#include <omphalos/netaddrs.h>
#include <omphalos/omphalos.h>
#include <ui/notcurses/util.h>
#include <ui/notcurses/core.h>
#include <ui/notcurses/iface.h>
#include <omphalos/interface.h>
#include <ui/notcurses/network.h>
#include <ui/notcurses/channels.h>

#define ERREXIT endwin() ; fprintf(stderr, "notcurses failure|%s|%d\n", __func__, __LINE__); abort() ; goto err

#define PANEL_STATE_INITIALIZER { .n = NULL, .ysize = -1, }

static struct panel_state help = PANEL_STATE_INITIALIZER;
static struct panel_state diags = PANEL_STATE_INITIALIZER;
static struct panel_state details = PANEL_STATE_INITIALIZER;
static struct panel_state network = PANEL_STATE_INITIALIZER;
static struct panel_state bridging = PANEL_STATE_INITIALIZER;
static struct panel_state channels = PANEL_STATE_INITIALIZER;
static struct panel_state environment = PANEL_STATE_INITIALIZER;

struct ncreel *reel = NULL;

static int rows = -1;
static int cols = -1;
static struct ncplane* stdn;
static struct panel_state *active;

// FIXME granularize things, make packet handler iret-like
static pthread_mutex_t bfl = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static pthread_t inputtid;

// Old host versioning display info
static const char *glibc_version,*glibc_release; // Currently unused
static struct utsname sysuts; // Currently unused

static inline void
lock_notcurses(void){
  pthread_mutex_lock(&bfl);
}

static inline void
unlock_notcurses(void){
  if(active){
    ncplane_move_top(active->n);
  }
  screen_update();
  pthread_mutex_unlock(&bfl);
}

// NULL fmt clears the status bar. wvstatus is an unlocked entry point, and
// thus calls screen_update() on exit.
static int
wvstatus(struct ncplane *w, const char *fmt, va_list va){
  int ret;

  lock_notcurses();
  ret = wvstatus_locked(w, fmt, va);
  if(diags.n && fmt){
    ret |= update_diags_locked(&diags);
  }
  unlock_notcurses();
  return ret;
}

// NULL fmt clears the status bar. wstatus is an unlocked entry point, and thus
// calls screen_update() on exit.
static int
wstatus(struct ncplane *w, const char *fmt, ...){
  va_list va;
  int ret;

  va_start(va, fmt);
  ret = wvstatus(w, fmt, va); // calls screen_update()
  va_end(va);
  return ret;
}

static void
resize_screen_locked(void){
  int dimy, dimx;
  notcurses_refresh(NC, &dimy, &dimx);
  ncplane_resize_simple(ncreel_plane(reel), dimy - 1, dimx);
  ncreel_redraw(reel);
  draw_main_window(stdn);
}

struct fs_input_marshal {
  struct notcurses *nc;
  pthread_t maintid;
};


static void
toggle_panel(struct ncplane *w, struct panel_state *ps, int (*psfxn)(struct ncplane *, struct panel_state *)){
  if(ps->n){
    hide_panel_locked(ps);
    active = NULL;
  }else{
    hide_panel_locked(active);
    active = ((psfxn(w, ps) == 0) ? ps : NULL);
  }
}

// Only meaningful if there are both interfaces and a subdisplay
static void
toggle_focus(void){
}

// Only meaningful if there are both interfaces and a subdisplay
static void
toggle_subwindow_pinning(void){
}

static void *
input_thread(void *unsafe_marsh){
  struct fs_input_marshal *nim = unsafe_marsh;
  struct notcurses *nc = nim->nc;
  uint32_t ch;
  ncinput ni;

  active = NULL; // No subpanels initially
  while((ch = notcurses_getc_blocking(nc, &ni)) != 'q' && ch != 'Q'){
  switch(ch){
    case NCKEY_HOME:
      lock_notcurses();
      if(selecting()){
        use_first_node_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_END:
      lock_notcurses();
      if(selecting()){
        use_last_node_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_PGUP:
      lock_notcurses();
      if(selecting()){
        use_prev_nodepage_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_PGDOWN:
      lock_notcurses();
      if(selecting()){
        use_next_nodepage_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_UP: case 'k':
      lock_notcurses();
      if(!selecting()){
        ncreel_prev(reel);
      }else{
        use_prev_node_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_DOWN: case 'j':
      lock_notcurses();
      if(!selecting()){
        ncreel_next(reel);
      }else{
        use_next_node_locked();
      }
      unlock_notcurses();
      break;
    case NCKEY_RESIZE:
      lock_notcurses();{
        resize_screen_locked();
      }unlock_notcurses();
      break;
    case 9: // Tab FIXME
      lock_notcurses();
        toggle_focus();
      unlock_notcurses();
      break;
    case 12: // Ctrl-L FIXME
      lock_notcurses();{
        notcurses_refresh(NC, &rows, &cols);
      }unlock_notcurses();
      break;
    case '\r': case '\n': case NCKEY_ENTER:
      lock_notcurses();{
        select_iface_locked();
      }unlock_notcurses();
      break;
    case NCKEY_ESC: case NCKEY_BACKSPACE:
      lock_notcurses();{
        deselect_iface_locked();
      }unlock_notcurses();
      break;
    case 'l':
      lock_notcurses();
        toggle_panel(stdn, &diags, display_diags_locked);
      unlock_notcurses();
      break;
    case 'D':
      lock_notcurses();
        resolve_selection(stdn);
      unlock_notcurses();
      break;
    case 'r':
      lock_notcurses();
        reset_current_interface_stats(stdn);
      unlock_notcurses();
      break;
    case 'P':
      lock_notcurses();
        toggle_subwindow_pinning();
      unlock_notcurses();
      break;
    case 'p':
      lock_notcurses();
        toggle_promisc_locked(stdn);
      unlock_notcurses();
      break;
    case 'd':
      lock_notcurses();
        down_interface_locked(stdn);
      unlock_notcurses();
      break;
    case 's':
      lock_notcurses();
        sniff_interface_locked(stdn);
      unlock_notcurses();
      break;
    case '+':
    case NCKEY_RIGHT:
      lock_notcurses();
        expand_iface_locked();
      unlock_notcurses();
      break;
    case '-':
    case NCKEY_LEFT:
      lock_notcurses();
        collapse_iface_locked();
      unlock_notcurses();
      break;
    case 'v':{
      lock_notcurses();
        toggle_panel(stdn, &details, display_details_locked);
      unlock_notcurses();
      break;
    }case 'n':{
      lock_notcurses();
        toggle_panel(stdn, &network, display_network_locked);
      unlock_notcurses();
      break;
    }case 'e':{
      lock_notcurses();
        toggle_panel(stdn, &environment, display_env_locked);
      unlock_notcurses();
      break;
    }case 'w':{
      lock_notcurses();
        toggle_panel(stdn, &channels, display_channels_locked);
      unlock_notcurses();
      break;
    }case 'b':{
      lock_notcurses();
        toggle_panel(stdn, &bridging, display_bridging_locked);
      unlock_notcurses();
      break;
    }case 'h':{
      lock_notcurses();
        toggle_panel(stdn, &help, display_help_locked);
      unlock_notcurses();
      break;
    }default:{
      const char *hstr = !help.n ? " ('h' for help)" : "";
      // wstatus() locks/unlocks, and calls screen_update()
      if(isprint(ch)){
        wstatus(stdn, "unknown command '%c'%s", ch, hstr);
      }else{
        wstatus(stdn, "unknown scancode %d%s", ch, hstr);
      }
      break;
    }
  }
  }
  wstatus(stdn, "%s", "shutting down");
  // we can't use raise() here, as that sends the signal only
  // to ourselves, and we have it masked.
  pthread_kill(nim->maintid, SIGINT);
  pthread_exit(NULL);
}

// Cleanup which ought be performed even if we had a failure elsewhere, or
// indeed never started.
static int
mandatory_cleanup(struct notcurses *nc){
  int ret = 0;

  pthread_mutex_lock(&bfl);
  ret = notcurses_stop(nc);
  pthread_mutex_unlock(&bfl);
  return ret;
}

static struct notcurses*
notcurses_setup(void){
  struct fs_input_marshal *nim;
  struct notcurses *nc = NULL;
  const char *errstr = NULL;
  struct notcurses_options nopts = {
    //.loglevel = NCLOGLEVEL_TRACE,
    .flags = NCOPTION_INHIBIT_SETLOCALE,
  };

  fprintf(stderr, "Entering fullscreen mode...\n");
  if((nc = notcurses_init(&nopts, NULL)) == NULL){
    fprintf(stderr, "Couldn't initialize notcurses\n");
    return NULL;
  }
  stdn = notcurses_stddim_yx(nc, &rows, &cols);
  if(setup_statusbar(cols)){
    errstr = "Couldn't setup status bar\n";
    goto err;
  }
  ncplane_options rnopts = {
    .x = NCALIGN_CENTER,
    .rows = rows - 1,
    .cols = cols,
    .name = "reel",
    .flags = NCPLANE_OPTION_HORALIGNED,
  };
  struct ncplane *reelncp = ncplane_create(stdn, &rnopts);
  if(reelncp == NULL){
    errstr = "Couldn't create main reel plane\n";
    goto err;
  }
  ncreel_options ropts = {
    .bordermask = 0xf,
    .tabletmask = 0xf,
    .flags = NCREEL_OPTION_INFINITESCROLL | NCREEL_OPTION_CIRCULAR,
  };
  reel = ncreel_create(reelncp, &ropts);
  if(reel == NULL){
    errstr = "Couldn't create main reel\n";
    goto err;
  }
  if(draw_main_window(stdn)){
    errstr = "Couldn't use notcurses\n";
    goto err;
  }
  if((nim = malloc(sizeof(*nim))) == NULL){
    goto err;
  }
  nim->nc = nc;
  nim->maintid = pthread_self();
  if(pthread_create(&inputtid, NULL, input_thread, nim)){
    errstr = "Couldn't create UI thread\n";
    free(nim);
    goto err;
  }
  return nc;

err:
  mandatory_cleanup(nc);
  fprintf(stderr, "%s", errstr);
  return NULL;
}

static void
packet_callback(omphalos_packet *op){
  pthread_mutex_lock(&bfl); // don't always want screen_update()
  if(packet_cb_locked(op->i, op, &details)){
    if(active){
      ncplane_move_top(active->n);
    }
    screen_update();
  }
  pthread_mutex_unlock(&bfl);
}

static void *
interface_callback(interface *i, void *unsafe){
  void *r;

  lock_notcurses();
    r = interface_cb_locked(reel, i, unsafe, &details);
  unlock_notcurses();
  return r;
}

static void *
wireless_callback(interface *i, unsigned wcmd __attribute__ ((unused)), void *unsafe){
  void *r;

  lock_notcurses();
    r = interface_cb_locked(reel, i, unsafe, &details);
  unlock_notcurses();
  return r;
}

static void *
service_callback(const interface *i, struct l2host *l2, struct l3host *l3, struct l4srv *l4){
  void *ret;

  pthread_mutex_lock(&bfl);
  if( (ret = service_callback_locked(i, l2, l3, l4)) ){
    if(active){
      ncplane_move_top(active->n);
    }
    screen_update();
  }
  pthread_mutex_unlock(&bfl);
  return ret;
}

static void *
host_callback(const interface *i, struct l2host *l2, struct l3host *l3){
  void *ret;

  pthread_mutex_lock(&bfl);
  if( (ret = host_callback_locked(i, l2, l3)) ){
    if(active){
      ncplane_move_top(active->n);
    }
    screen_update();
  }
  pthread_mutex_unlock(&bfl);
  return ret;
}

static void *
neighbor_callback(const interface *i, struct l2host *l2){
  void *ret;

  pthread_mutex_lock(&bfl);
  if( (ret = neighbor_callback_locked(i, l2)) ){
    if(active){
      ncplane_move_top(active->n);
    }
    screen_update();
  }
  pthread_mutex_unlock(&bfl);
  return ret;
}

static void
interface_removed_callback(const interface *i __attribute__ ((unused)), void *unsafe){
  lock_notcurses();
    interface_removed_locked(reel, unsafe, details.n ? &active : NULL);
  unlock_notcurses();
}

static void
vdiag_callback(const char *fmt, va_list v){
  wvstatus(stdn, fmt, v);
}

static void
network_callback(void){
  lock_notcurses();
    if(active == &network){
      update_network_details(network.n);
    }
  unlock_notcurses();
}

int main(int argc, char * const *argv){
  const char *codeset;
  omphalos_ctx pctx;

  if(setlocale(LC_ALL, "") == NULL || ((codeset = nl_langinfo(CODESET)) == NULL)){
    fprintf(stderr, "Couldn't initialize locale (%s?)\n", strerror(errno));
    return EXIT_FAILURE;
  }
  sigset_t sigmask;
  // ensure SIGWINCH is delivered only to a thread doing input
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGWINCH);
  pthread_sigmask(SIG_SETMASK, &sigmask, NULL);
  if(uname(&sysuts)){
    fprintf(stderr, "Couldn't get OS info (%s?)\n", strerror(errno));
    return EXIT_FAILURE;
  }
  glibc_version = gnu_get_libc_version();
  glibc_release = gnu_get_libc_release();

  if(omphalos_setup(argc, argv, &pctx)){
    return EXIT_FAILURE;
  }
  pctx.iface.packet_read = packet_callback;
  pctx.iface.iface_event = interface_callback;
  pctx.iface.iface_removed = interface_removed_callback;
  pctx.iface.vdiagnostic = vdiag_callback;
  pctx.iface.wireless_event = wireless_callback;
  pctx.iface.srv_event = service_callback;
  pctx.iface.neigh_event = neighbor_callback;
  pctx.iface.host_event = host_callback;
  pctx.iface.network_event = network_callback;
  if((NC = notcurses_setup()) == NULL){
    return EXIT_FAILURE;
  }
  if(omphalos_init(&pctx)){
    int err = errno;

    mandatory_cleanup(NC);
    fprintf(stderr, "Error in omphalos_init() (%s?)\n", strerror(err));
    return EXIT_FAILURE;
  }
  omphalos_cleanup(&pctx);
  if(mandatory_cleanup(NC)){
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
