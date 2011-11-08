#ifndef OMPHALOS_INOTIFY
#define OMPHALOS_INOTIFY

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*watchcbfxn)(const char *);

int watch_init(void);
int watch_file(const char *,watchcbfxn);
int handle_watch_event(int);
int watch_stop(void);

#ifdef __cplusplus
}
#endif

#endif
