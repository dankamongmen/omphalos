#include <stdlib.h>
#include <omphalos/udev.h>
#include <omphalos/diag.h>
#include <omphalos/sysfs.h>
#include <omphalos/interface.h>

#define SYSFS_PATH_MAX    256
#define SYSFS_NAME_LEN    64

// returns one of "pci" or "usb"
const char *lookup_bus(int netdev, topdev_info *tinf){
  if(find_net_device(netdev, tinf)){
    return NULL;
  }
  return "pci"; // FIXME no!
}
