#include <stdlib.h>
#include <omphalos/pci.h>
#include <omphalos/sysfs.h>
#include <sysfs/libsysfs.h>
#include <omphalos/interface.h>

// PCIe devices show up as PCI devices; the /bus/pci_express entries in sysfs
// are all about PCIe routing, not end devices.
static struct bus {
	const char *bus;
	int (*bus_lookup)(const char *,struct topdev_info *);
} buses[] = {
	{ "pci",	find_pci_device,	},
	{ "usb",	NULL,			},
	{ NULL,		NULL,			}
};

const char *lookup_bus(const char *dname,topdev_info *tinf){
	struct sysfs_device *dev;
	const struct bus *b;

	free(tinf->devname);
	tinf->devname = NULL;
	for(b = buses ; b->bus ; ++b){
		if( (dev = sysfs_open_device(b->bus,dname)) ){
			if(b->bus_lookup){
				b->bus_lookup(dname,tinf);
			}
			sysfs_close_device(dev);
			return b->bus;
		}
	}
	return NULL;
}
