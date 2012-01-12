#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <wctype.h>
#include <inttypes.h>
#include <omphalos/usb.h>
#include <omphalos/diag.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

/* LibUSB 1.0 implementation. Unused -- LibUSB is unsuitable for inclusion.
#include <libusb-1.0/libusb.h>
static libusb_context *usb;

int init_usb_support(void){
	if(libusb_init(&usb)){
		return -1;
	}
	return 0;
}

int stop_usb_support(void){
	if(usb){
		libusb_exit(usb);
		usb = NULL;
	}
	return 0;
}

static int
get_ul_token(const char **str,char tok,unsigned long *ul){
	char *e;

	if(**str == '-'){ // guard against strotul() '-'-parsing behavior
		return -1;
	}
	if((*ul = strtoul(*str,&e,16)) == ULONG_MAX && errno == ERANGE){
		return -1;
	}
	if(*e != tok){
		return -1;
	}
	*str = e + 1;
	return 0;
}

// feed it the bus id as provided by libsysfs
static libusb_device_handle *
lookup(const char *busid,struct libusb_device_descriptor *desc){
	libusb_device_handle *handle;
	unsigned long bus,dev,port;
	libusb_device **list;
	const char *cur;
	ssize_t s;

	cur = busid; // sysfs form: bus-port.dev:x.y
	if(get_ul_token(&cur,'-',&bus) || get_ul_token(&cur,'.',&port) ||
			get_ul_token(&cur,':',&dev)){
		return NULL;
	}
	// Error, or no USB devices connected to the system. Since we were
	// triggered off a discovered device, following a bus lookup via sysfs,
	// there certainly ought be a USB device connected.
	if((s = libusb_get_device_list(usb,&list)) <= 0){
		return NULL;
	}
	handle = NULL;
	while(--s >= 0){
		if(libusb_get_bus_number(list[s]) != bus){
			continue;
		}
		if(libusb_get_device_address(list[s]) != dev){
			continue;
		}
		if(libusb_get_device_descriptor(list[s],desc)){
			break;
		}
		if(libusb_open(list[s],&handle)){
			break;
		}
		break;
	}
	libusb_free_device_list(list,1);
	return handle;
}

int find_usb_device(const char *busid,struct sysfs_device *sd __attribute__ ((unused)),
				topdev_info *tinf){
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle;
	unsigned char buf[128]; // FIXME
	int l;

	if((handle = lookup(busid,&desc)) == NULL){
		return -1;
	}
	if((l = libusb_get_string_descriptor_ascii(handle,desc.iManufacturer,buf,sizeof(buf))) <= 0){
		libusb_close(handle);
		return -1;
	}
	buf[l] = ' ';
	if((l = libusb_get_string_descriptor_ascii(handle,desc.iProduct,buf + l,sizeof(buf) - l)) <= 0){
		libusb_close(handle);
		return -1;
	}
	libusb_close(handle);
	if((tinf->devname = strdup((const char *)buf)) == NULL){
		return -1;
	}
	return 0;
}
*/

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

// USB ID database implementation
int init_usb_support(const char *fn){
	if(watch_file(fn,parse_usbids_file)){
		return -1;
	}
	return 0;
}

int stop_usb_support(void){
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
	return 0;
}

// libsysfs implementation
#include <sysfs/libsysfs.h>

int find_usb_device(const char *busid __attribute__ ((unused)),
		struct sysfs_device *sd,topdev_info *tinf){
	struct sysfs_attribute *attr;
	struct sysfs_device *parent;
	unsigned long val;
	wchar_t *tmp;
	char *e;

	if((parent = sysfs_get_device_parent(sd)) == NULL){
		return -1;
	}
	if((attr = sysfs_get_device_attr(parent,"idVendor")) == NULL){
		return -1;
	}
	if((val = strtoul(attr->value,&e,16)) > 0xffffu || *e != '\n' || e == attr->value ||
					!vendors[val].name){
		if((attr = sysfs_get_device_attr(parent,"manufacturer")) == NULL){
			return -1;
		}
		if((tinf->devname = malloc(sizeof(wchar_t) * (strlen(attr->value) + 1))) == NULL){
			return -1;
		}
		assert(mbstowcs(tinf->devname,attr->value,strlen(attr->value)) == strlen(attr->value));
	}else{
		const struct usb_vendor *vend;

		if((tinf->devname = wcsdup(vendors[val].name)) == NULL){
			return -1;
		}
	       	vend = &vendors[val];
		if((attr = sysfs_get_device_attr(parent,"idProduct")) == NULL){
			return -1;
		}
		if((val = strtoul(attr->value,&e,16)) <= 0xffffu && *e == '\n' && e != attr->value){
			unsigned dev;

			for(dev = 0 ; dev < vend->devcount ; ++dev){
				if(vend->devices[dev].devid == val){
					size_t wlen = wcslen(tinf->devname);

					if((tmp = realloc(tinf->devname,sizeof(*tinf->devname) * (wlen + wcslen(vend->devices[dev].name) + 2))) == NULL){
						free(tinf->devname);
						tinf->devname = NULL;
						return -1;
					}
					tmp[wlen] = L' ';
					tmp[wlen + 1] = L'\0';
					wcscat(tmp,vend->devices[dev].name);
					tinf->devname = tmp;
					return 0;
				}
			}
		}
	}
	if((attr = sysfs_get_device_attr(parent,"product")) == NULL){
		free(tinf->devname);
		tinf->devname = NULL;
		return -1;
	}
	if((tmp = realloc(tinf->devname,sizeof(wchar_t) * (wcslen(tinf->devname) + strlen(attr->value) + 2))) == NULL){
		free(tinf->devname);
		tinf->devname = NULL;
		return -1;
	}
	// They come with a newline at the end, argh!
	tmp[wcslen(tmp) - 1] = L' ';
	mbstowcs(tmp + wcslen(tmp) + 1,attr->value,strlen(attr->value));
	tmp[wcslen(tmp)] = L'\0';
	tinf->devname = tmp;
	return 0;
}
