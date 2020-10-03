#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <wctype.h>
#include <stdlib.h>
#include <libudev.h>
#include <inttypes.h>
#include <omphalos/udev.h>
#include <omphalos/diag.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

static struct udev* udev;

int init_udev_support(void){
  if((udev = udev_new()) == NULL){
    diagnostic("Couldn't initialize libudev");
    return -1;
  }
	return 0;
}

int stop_udev_support(void){
  udev_unref(udev);
	return 0;
}

static const char*
udev_dev_tinf(struct udev_device* dev, struct topdev_info* tinf){
  const char* veasy = udev_device_get_property_value(dev, "ID_VENDOR_FROM_DATABASE");
  if(veasy){
		if((tinf->devname = malloc(sizeof(wchar_t) * (strlen(veasy) + 1))) == NULL){
			return NULL;
		}
    mbstowcs(tinf->devname, veasy, strlen(veasy) + 1);
    veasy = udev_device_get_property_value(dev, "ID_MODEL_FROM_DATABASE");
    if(veasy){
      size_t wlen = wcslen(tinf->devname);
      wchar_t *tmp;
      if((tmp = realloc(tinf->devname, sizeof(*tinf->devname) * (wlen + strlen(veasy) + 2))) == NULL){
        free(tinf->devname);
        tinf->devname = NULL;
        return NULL;
      }
      tmp[wlen] = L' ';
      tmp[wlen + 1] = L'\0';
      mbstowcs(tmp + wlen + 1, veasy, strlen(veasy) + 1);
      tinf->devname = tmp;
    }
    return udev_device_get_property_value(dev, "ID_BUS");
  }
	return NULL;
}

// returns one of "pci" or "usb" (or NULL)
char *lookup_bus(int netdev, topdev_info *tinf){
	struct udev_device *dev;

  char devstr[20];
  int sp = snprintf(devstr, sizeof(devstr), "n%d", netdev);
  if(sp < 0 || (size_t)sp >= sizeof(devstr)){
    return NULL;
  }
  if((dev = udev_device_new_from_device_id(udev, devstr)) == NULL){
    diagnostic("Udev failed to return netdevid %d", netdev);
    return NULL;
  }
  const char* cret = udev_dev_tinf(dev, tinf);
  char* ret = cret ? strdup(cret) : NULL;
  udev_device_unref(dev);
  return ret;
}
