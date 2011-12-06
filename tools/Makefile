# warning: this shit's broken. it's not integrated with the main make.
# unsupported throughout!

all: nl80211

nl80211: nl80211.c ../out/src/omphalos/nl80211.o
	gcc -o $@ -I../src/ $^ $(shell pkg-config --libs libnl-3.0)

clean:
	rm nl80211
