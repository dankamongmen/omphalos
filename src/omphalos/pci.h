#ifndef OMPHALOS_PCI
#define OMPHALOS_PCI

#ifdef __cplusplus
extern "C" {
#endif

struct topdev_info;

int init_pci_support(void);
int stop_pci_support(void);

// feed it the bus id as provided by libsysfs
int find_pci_device(const char *,struct topdev_info *);

#ifdef __cplusplus
}
#endif

#endif
