#include <omphalos/sysfs.h>
#include <sysfs/libsysfs.h>

static const char *busids[] = {
	"pci",
	"pci_express",
	"usb",
	NULL
};

const char *lookup_bus(const char *dname){
	struct sysfs_device *dev;
	const char **id;

	for(id = busids ; *id ; ++id){
		if( (dev = sysfs_open_device(*id,dname)) ){
			sysfs_close_device(dev);
			return *id;
		}
	}
	return NULL;
}
