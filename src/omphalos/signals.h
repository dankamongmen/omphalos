#ifndef OMPHALOS_SIGNALS
#define OMPHALOS_SIGNALS

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>

// Add the cancellation signals to the current signal mask.
int mask_cancel_sigs(void);

// Restore the signal mask as saved by mask_cancel_sigs().
int restore_sigmask(void);

int restore_sighandler(void);
int setup_sighandler(void (*)(int));

#ifdef __cplusplus
}
#endif

#endif
