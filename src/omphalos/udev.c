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

static struct usb_vendor {
	wchar_t *name;
	unsigned devcount;
	struct usb_device {
		wchar_t *name;
		unsigned devid;
	} *devices;
} vendors[65536];

// Who knows what the hell character set the input file might be. It ought be
// UTF-8, but the site serves ISO-8859-1 with invalid UTF-8 characters. FIXME
static int
parse_usbids_file(const char *fn){
	int line = 0,devs = 0,vends = 0,curvendor = -1;
	struct timeval t0,t1,t2;
	wchar_t buf[1024]; // FIXME ugh!
	FILE *fp;

	gettimeofday(&t0,NULL);
	if((fp = fopen(fn,"r")) == NULL){
		diagnostic("Couldn't open USB ID db at %s (%s?)",fn,strerror(errno));
		return -1;
	}
	while(fgetws(buf,sizeof(buf) / sizeof(*buf),fp)){
		wchar_t *c,*e,*nl,*tok;
		uintmax_t val;

		++line;
		// Verify and strip the trailing newline
		nl = wcschr(buf,L'\n');
		if(!nl){
			goto formaterr;
		}
		*nl = L'\0';
		// Skip leading whitespace
		c = buf;
		while(iswspace(*c)){
			++c;
		}
		// Ignore comments and blank lines
		if(*c == L'#' || *c == L'\0'){
			continue;
		}
		// Unfortunately, there's a bunch of crap added on to the end
		// of the usb.ids file (USB classes, etc). We throw it away...
		// We ought have 4 hexadecimal digits followed by two spaces...
		if((val = wcstoumax(c,&e,16)) > 0xffff || e != c + 4){
			// FIXME goto formaterr;
			continue;
		}
		if(val > sizeof(vendors) / sizeof(*vendors)){
			goto formaterr;
			continue;
		}
		if(*e++ != ' ' || *e++ != ' '){
			goto formaterr;
			continue;
		}
		// ...followed by a string.
		if((tok = wcsdup(e)) == NULL){
			diagnostic("Error allocating USB ID (%s?)",strerror(errno));
			fclose(fp);
			return -1;
		}
		// If we're at the beginning of a line, we're a new Vendor
		if(c == buf){
			curvendor = val;
			if(vendors[curvendor].name){
				goto formaterr;	// duplicate definition
			}
			vendors[curvendor].name = tok;
			++vends;
		}else{
			struct usb_vendor *vend;
			struct usb_device *dev;

			// Ought have a vendor context
			if(curvendor < 0 || vendors[curvendor].name == NULL){
				goto formaterr;
			}
			vend = &vendors[curvendor];
			dev = realloc(vend->devices,sizeof(*vend->devices) * (vend->devcount + 1));
			if(dev == NULL){
				diagnostic("Error allocating USB array (%s?)",strerror(errno));
				fclose(fp);
				return -1;
			}
			vend->devices = dev;
			vend->devices[vend->devcount].name = tok;
			vend->devices[vend->devcount].devid = val;
			++vend->devcount;
			++devs;
		}
		continue;

formaterr:
		diagnostic("Error at line %d of %s",line,fn);
		fclose(fp);
		return -1;
	}
	if(ferror(fp)){
		diagnostic("Error reading USB ID db at %s (%s?)",fn,strerror(errno));
		fclose(fp);
		return -1;
	}
	if(fclose(fp)){
		diagnostic("Couldn't close USB ID db at %s (%s?)",fn,strerror(errno));
		return -1;
	}
	gettimeofday(&t1,NULL);
	timersub(&t1,&t0,&t2);
	diagnostic("Reloaded %d vendor%s and %d USB device%s from %s in %lu.%07lus",
			vends,vends == 1 ? "" : "s",devs,devs == 1 ? "" : "s",fn,
			t2.tv_sec,t2.tv_usec);
	return 0;
}

// USB ID database implementation + libudev-based lookup
int init_udev_support(const char *fn){
  if((udev = udev_new()) == NULL){
    diagnostic("Couldn't initialize libudev");
    return -1;
  }
	watch_file(fn, parse_usbids_file); // don't require usb.ids
	return 0;
}

int stop_udev_support(void){
	unsigned idx;

	for(idx = 0 ; idx < sizeof(vendors) / sizeof(*vendors) ; ++idx){
		struct usb_vendor *uv;

		uv = &vendors[idx];
		if(uv->name){
			while(uv->devcount--){
				free(uv->devices[uv->devcount].name);
			}
			free(uv->devices);
			free(uv->name);
		}
	}
  udev_unref(udev);
	return 0;
}

static const char*
udev_dev_tinf(struct udev_device* dev, struct topdev_info* tinf){
  unsigned long val;
	wchar_t *tmp;
	char *e;
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
  const char* vendstr = udev_device_get_property_value(dev, "ID_VENDOR_ID");
	if(!vendstr || (val = strtoul(vendstr, &e, 16)) > 0xffffu || *e
     || e == vendstr || !vendors[val].name){
    return NULL;
	}
  const struct usb_vendor *vend;
  if((tinf->devname = wcsdup(vendors[val].name)) == NULL){
    return NULL;
  }
  vend = &vendors[val];
  const char* modstr = udev_device_get_property_value(dev, "ID_MODEL_ID");
  if(!modstr || (val = strtoul(modstr, &e, 16)) > 0xffffu || *e || e == modstr){
    return NULL; // leave partial devname
  }
  for(unsigned devno = 0 ; devno < vend->devcount ; ++devno){
    if(vend->devices[devno].devid == val){
      size_t wlen = wcslen(tinf->devname);
      if((tmp = realloc(tinf->devname, sizeof(*tinf->devname) * (wlen + wcslen(vend->devices[devno].name) + 2))) == NULL){
        return NULL; // leave partial devname
      }
      tmp[wlen] = L' ';
      tmp[wlen + 1] = L'\0';
      wcscat(tmp, vend->devices[devno].name);
      tinf->devname = tmp;
      return udev_device_get_property_value(dev, "ID_BUS");
    }
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