#ifndef OMPHALOS_BLUETOOTH
#define OMPHALOS_BLUETOOTH

#ifdef __cplusplus
extern "C" {
#endif

struct omphalos_iface;

int discover_bluetooth(const struct omphalos_iface *);

#ifdef __cplusplus
}
#endif

#endif
