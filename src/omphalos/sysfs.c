#include <stdlib.h>
#include <omphalos/usb.h>
#include <omphalos/pci.h>
#include <omphalos/sysfs.h>
#include <omphalos/interface.h>

// PCIe devices show up as PCI devices; the /bus/pci_express entries in sysfs
// are all about PCIe routing, not end devices.
static struct bus {
	const char *bus;
	int (*bus_lookup)(const char *,struct sysfs_device *,
				struct topdev_info *);
} buses[] = {
	{ "pci",	find_pci_device,	},
	{ "usb",	find_usb_device,	},
	{ NULL,		NULL,			}
};

struct sysfs_attribute* sysfs_get_device_attr(struct sysfs_device *dev, const char *name){
  fprintf(stderr, "Want sysfs attr %s from dev %p\n", name, dev);
  return NULL;
}

struct sysfs_device *sysfs_open_device(const char *bus, const char *bus_id){
  fprintf(stderr, "OPENING sysfs %s %s\n", bus, bus_id);
  return NULL;
}

struct sysfs_device *sysfs_get_device_parent(struct sysfs_device *dev){
  fprintf(stderr, "GET DEVICE FOR %p\n", dev);
  return NULL;
}

void sysfs_close_device(struct sysfs_device* dev){
  fprintf(stderr, "CLOSING sysfs %p\n", dev);
}

const char *lookup_bus(const char *dname, topdev_info *tinf){
	struct sysfs_device *dev;
	const struct bus *b;

	free(tinf->devname);
	tinf->devname = NULL;
	for(b = buses ; b->bus ; ++b){
fprintf(stderr, "WANT TO OPEN BUS [%s] DNAME [%s]\n", b->bus, dname);
		if( (dev = sysfs_open_device(b->bus, dname)) ){
			if(b->bus_lookup){
				b->bus_lookup(dname,dev,tinf);
			}
			sysfs_close_device(dev);
			return b->bus;
		}
	}
	return NULL;
}
