#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <wctype.h>
#include <inttypes.h>
#include <omphalos/usb.h>
#include <omphalos/diag.h>
#include <sysfs/libsysfs.h>
#include <omphalos/inotify.h>
#include <omphalos/omphalos.h>
#include <omphalos/interface.h>

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
