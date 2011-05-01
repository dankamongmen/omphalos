.DELTE_ON_ERROR:
.DEFAULT_GOAL:=test
.PHONY: all bin lib doc livetest test clean install uninstall \
	bless sudobless

VERSION=0.0.1

OUT:=out
SRC:=src
DOC:=doc
PROJ:=omphalos
TAGS:=$(OUT)/tags
OMPHALOS:=$(OUT)/$(PROJ)/$(PROJ)
ADDCAPS:=tools/addcaps

UI:=ncurses tty
BIN:=$(addprefix $(OMPHALOS)-,$(UI))

CFLAGS+=-I$(SRC) -pthread -D_GNU_SOURCE -fpic -I$(SRC)/lib$(PROJ) -fvisibility=hidden -O2 -Wall -W -Werror -g
LFLAGS+=-Wl,-O,--default-symver,--enable-new-dtags,--as-needed,--warn-common
LFLAGS+=-lpcap -lcap
CTAGS?=$(shell (which ctags || echo ctags) 2> /dev/null)
XSLTPROC?=$(shell (which xsltproc || echo xsltproc) 2> /dev/null)
INSTALL?=install -v
PREFIX?=/usr/local
ifeq ($(UNAME),FreeBSD)
DOCPREFIX?=$(PREFIX)/man
MANBIN?=makewhatis
LDCONFIG?=ldconfig -m
else
DOCPREFIX?=$(PREFIX)/share/man
MANBIN?=mandb
LDCONFIG?=ldconfig
endif

MAN3SRC:=$(wildcard $(DOC)/man/man3/*)
MAN3:=$(addprefix $(OUT)/,$(MAN3SRC:%.xml=%.3$(PROJ)))

all: $(TAGS) lib bin

bin: $(BIN)

doc: $(MAN3)

lib: $(LIB)

TESTPCAPS:=$(wildcard test/*)

CSRCDIRS:=$(wildcard $(SRC)/*)
CSRCS:=$(shell find $(CSRCDIRS) -type f -iname \*.c -print)
CINCS:=$(shell find $(CSRCDIRS) -type f -iname \*.h -print)
COBJS:=$(addprefix $(OUT)/,$(CSRCS:%.c=%.o))

# Various UI's plus the core make the binaries
COREOBJS:=$(filter $(OUT)/$(SRC)/$(PROJ)/%.o,$(COBJS))

# Requires CAP_NET_ADMIN privileges bestowed upon the binary
livetest: sudobless
	$(OMPHALOS)-ncurses

test: all $(TESTPCAPS)
	for i in $(TESTPCAPS) ; do $(OMPHALOS)-tty -f $$i ; done

$(OMPHALOS)-ncurses: $(COREOBJS) $(OUT)/$(SRC)/ui/ncurses.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lncurses

$(OMPHALOS)-tty: $(COREOBJS) $(OUT)/$(SRC)/ui/tty.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

$(OUT)/%.o: %.c $(CINCS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# Should the network be inaccessible, and local copies are installed, try:
#DOC2MANXSL?=--nonet
$(OUT)/%.1omphalos: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2MANXSL) $<

$(TAGS): $(wildcard $(SRC)/*/*.c) $(wildcard $(SRC)/*/*.h)
	@mkdir -p $(@D)
	$(CTAGS) -o $@ -R $(SRC)

clean:
	rm -rf $(OUT)

bless: all
	$(ADDCAPS) $(BIN)

sudobless: test
	sudo $(ADDCAPS) $(BIN)

install: all doc
	@mkdir -p $(PREFIX)/lib
	$(INSTALL) -m 0644 $(realpath $(LIB)) $(PREFIX)/lib
	@mkdir -p $(PREFIX)/include
	@$(INSTALL) -m 0644 $(wildcard $(SRC)/lib$(PROJ)/*.h) $(PREFIX)/include
	@mkdir -p $(DOCPREFIX)/man3
	@$(INSTALL) -m 0644 $(MAN3) $(DOCPREFIX)/man3
	@echo "Running $(LDCONFIG) $(PREFIX)/lib..." && $(LDCONFIG) $(PREFIX)/lib
	@echo "Running $(MANBIN) $(DOCPREFIX)..." && $(MANBIN) $(DOCPREFIX)

uninstall:
