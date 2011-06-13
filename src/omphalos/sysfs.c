#include <omphalos/pci.h>
#include <omphalos/sysfs.h>
#include <sysfs/libsysfs.h>

// PCIe devices show up as PCI devices; the /bus/pci_express entries in sysfs
// are all about PCIe routing, not end devices.
static struct bus {
	const char *bus;
	int (*bus_lookup)(const char *);
} buses[] = {
	{ "pci",	find_pci_device,	},
	{ "usb",	NULL,			},
	{ NULL,		NULL,			}
};

const char *lookup_bus(const char *dname){
	struct sysfs_device *dev;
	const struct bus *b;

	for(b = buses ; b->bus ; ++b){
		if( (dev = sysfs_open_device(b->bus,dname)) ){
			if(b->bus_lookup){
				b->bus_lookup(dname);
			}
			sysfs_close_device(dev);
			return b->bus;
		}
	}
	return NULL;
}
