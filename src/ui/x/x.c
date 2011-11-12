#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

int main(void){
	int scr,x,y,w,h,bw;
	Window win;
	Display *d;

	if(XInitThreads() == 0){
		return EXIT_FAILURE;
	}
	if((d = XOpenDisplay(NULL)) == NULL){
		return EXIT_FAILURE;
	}
	if(XNoOp(d) == 0){
		return EXIT_FAILURE;
	}
	scr = DefaultScreen(d);
	x = 0;
	y = 0;
	h = 100;
	w = 100;
	bw = 1;
	if((win = XCreateSimpleWindow(d,RootWindow(d,scr),x,y,h,w,bw,
				BlackPixel(d,scr),WhitePixel(d,scr))) == 0){
		return EXIT_FAILURE;
	}
	if(XMapWindow(d,win) == 0){
		return EXIT_FAILURE;
	}
	sleep(5);
	if(XCloseDisplay(d)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
