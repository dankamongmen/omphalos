#ifndef OMPHALOS_SIGNALS
#define OMPHALOS_SIGNALS

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

int restore_sighandler(void);
int setup_sighandler(void (*)(int));

#ifdef __cplusplus
}
#endif

#endif
