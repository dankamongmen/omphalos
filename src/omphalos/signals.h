#ifndef OMPHALOS_SIGNALS
#define OMPHALOS_SIGNALS

#ifdef __cplusplus
extern "C" {
#endif

int restore_sigmask(void);
int restore_sighandler(void);
int setup_sighandler(void (*)(int));

#ifdef __cplusplus
}
#endif

#endif
