#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <omphalos/diag.h>
#include <omphalos/omphalos.h>
#include <omphalos/bluetooth.h>

// Conflicts with our uint128 :(
//#include <bluetooth/bluetooth.h>

typedef struct {
	uint8_t b[6];
} __attribute__((packed)) bdaddr_t;
#define BTPROTO_HCI	1

#include <bluetooth/hci.h>
static struct {
	struct hci_dev_list_req list;
	struct hci_dev_req devlist[HCI_MAX_DEV];
} devreq;

int discover_bluetooth(void){
	int sd;

	if((sd = socket(AF_BLUETOOTH,SOCK_RAW,BTPROTO_HCI)) < 0){
		if(errno == EAFNOSUPPORT){
			diagnostic("No IEEE 802.15 (Bluetooth) support");
			return 0;
		}
		diagnostic("Couldn't get Bluetooth socket (%s?)",strerror(errno));
		return -1;
	}
	devreq.list.dev_num = sizeof(devreq.devlist) / sizeof(*devreq.devlist);
	if(ioctl(sd,HCIGETDEVLIST,&devreq)){
		diagnostic("Failure listing IEEE 802.15 (Bluetooth) devices (%s?)",strerror(errno));
		close(sd);
		return -1;
	}
	if(devreq.list.dev_num){
		// FIXME found bluetooth devices
	}
	close(sd);
	return 0;
}
