#ifndef OMPHALOS_POPEN
#define OMPHALOS_POPEN

#ifdef __cplusplus
extern "C" {
#endif

int popen_drain(const char *);
int vpopen_drain(const char *,...) __attribute__ ((format (printf,1,2)));

#ifdef __cplusplus
}
#endif

#endif
