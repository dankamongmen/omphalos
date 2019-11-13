#include <assert.h>
#include <string.h>
#include <net/if.h>
#include <ui/ncurses/core.h>
#include <ui/ncurses/util.h>
#include <ui/ncurses/iface.h>
#include <omphalos/wireless.h>
#include <omphalos/interface.h>
#include <ui/ncurses/channels.h>

static unsigned ifaces_used;

// Unfortunately, this assumes at least 70 columns (14 * 4 + IFNAMSIZ + 1).
#define FREQSPERROW 14  // FIXME do it dynamically based on cols

#define COLSPERFREQ 4 // FIXME based off < 1000 distinct licensed frequencies

// In addition to color changes,  each interface is represented by a different
// character. The interfaces supporting a frequency are displayed underneath,
// so we can only use as many wireless interfaces as columns taken up by
// frequencies under this scheme. All very brittle. FIXME
static const char iface_chars[COLSPERFREQ] = { '*',  '#',  '&',  '@',  };
static const struct iface_state *ifaces[COLSPERFREQ];

static int
channel_row(WINDOW *w, unsigned freqrow, int srow, int scol){
  unsigned f;

  assert(wmove(w, srow, scol + IFNAMSIZ + 2) == OK);
  for(f = freqrow * FREQSPERROW ; f < (freqrow + 1) * FREQSPERROW ; ++f){
    unsigned chan = wireless_chan_byidx(f);

    assert(chan < 1000);
    if(chan){
      assert(wprintw(w, " %3u", chan) == OK);
    }
  }
  return 0;
}

static inline int
iface_supports_freq(const struct iface_state *is, unsigned idx){
  if(is){
    return wireless_freq_supported_byidx(is->iface, idx) > 0.0;
  }
  return 0;
}

static int
iface_row(WINDOW *w, unsigned freqrow, int srow, int scol){
  unsigned f;

  if(freqrow < sizeof(ifaces) / sizeof(*ifaces) && ifaces[freqrow]){
    assert(mvwprintw(w, srow, scol, "%c%-*.*s ", iface_chars[freqrow],
      IFNAMSIZ, IFNAMSIZ, ifaces[freqrow]->iface->name) == OK);
  }else{
    assert(wmove(w, srow, scol + 1 + IFNAMSIZ + 1) == OK);
  }
  for(f = freqrow * FREQSPERROW ; f < (freqrow + 1) * FREQSPERROW ; ++f){
    char str[COLSPERFREQ + 1], *s;
    unsigned d;

    s = str;
    for(d = 0 ; d < sizeof(str) / sizeof(*str) - 1 ; ++d){
      if(iface_supports_freq(ifaces[d], f)){
        *s++ = iface_chars[d];
      }
    }
    *s = '\0';
    assert(wprintw(w, "%*.*s", COLSPERFREQ, COLSPERFREQ, str) == OK);
  }
  return 0;
}

static int
channel_details(WINDOW *w){
  unsigned freqs, freqrows;
  const int row = 1;
  int r, c, col,  z;

  getmaxyx(w, r, c);
  --r;
  --c;
  col = c - (START_COL + FREQSPERROW * 4 + IFNAMSIZ + 1);
  assert(col >= 0);
  assert(wattrset(w, SUBDISPLAY_ATTR) == OK);
  freqs = wireless_freq_count();
fprintf(stderr,  "count: %u\n",  freqs);
  freqrows = freqs / FREQSPERROW;
  if(freqs % FREQSPERROW){
    ++freqrows;
  }
  z = r - 2;
  int y = 1;
  while(z > 0){
fprintf(stderr,  "Y: %d Z: %d uhh: %d\n",  y,  z,  z > 0);
    iface_row(w,  freqrows - y,  row + z,  col);
    --z;
    channel_row(w,  freqrows - y,  row + z,  col);
    --z;
    ++y;
  }
  return OK;
}

int display_channels_locked(WINDOW *w, struct panel_state *ps){
  memset(ps, 0, sizeof(*ps));
  const int count = wireless_freq_count();
  int rows = count / FREQSPERROW;
  if(count % FREQSPERROW){
    ++rows;
  }
  if(new_display_panel(w, ps, rows + 6, 76, L"press 'w' to dismiss display")){
    goto err;
  }
  if(channel_details(panel_window(ps->p))){
    goto err;
  }
  return OK;

err:
  if(ps->p){
    WINDOW *psw = panel_window(ps->p);

    hide_panel(ps->p);
    del_panel(ps->p);
    delwin(psw);
  }
  memset(ps, 0, sizeof(*ps));
  return ERR;
}

int add_channel_support(struct iface_state *is){
  if(is->iface->settings_valid != SETTINGS_VALID_WEXT &&
      is->iface->settings_valid != SETTINGS_VALID_NL80211){
    return 0;
  }
  if(ifaces_used == sizeof(ifaces) / sizeof(*ifaces)){
    return 0;
  }
  ifaces[ifaces_used++] = is;
  // FIXME update if active!
  return 0;
}

int del_channel_support(struct iface_state *is){
  unsigned z;

  for(z = 0 ; z < ifaces_used ; ++z){
    if(ifaces[z] == is){
      if(z < ifaces_used - 1){
        memmove(ifaces + z, ifaces + z + 1, sizeof(*ifaces) * (ifaces_used - z - 1));
      }
      --ifaces_used;
      // FIXME update if active!
      break;
    }
  }
  return 0;
}
