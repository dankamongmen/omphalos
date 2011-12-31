# warning: this shit's broken. it's not integrated with the main make.
# unsupported throughout!

.PHONY: all up clean

all: nl80211

nl80211: nl80211.c $(wildcard ../out/src/omphalos/*.o)
	gcc -pthread -o $@ -I../src/ $^ $(shell pkg-config --libs libnl-3.0) -lcap -lpcap -lsysfs -lz -lpciaccess -liw

up:
	cd .. && make sudobless

clean:
	rm nl80211
