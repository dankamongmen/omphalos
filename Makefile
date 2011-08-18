.DELTE_ON_ERROR:
.DEFAULT_GOAL:=test
.PHONY: all bin lib doc livetest test valgrind clean clobber install uninstall
.PHONY:	bless sudobless

VERSION=0.0.1

OUT:=out
SRC:=src
DOC:=doc
PROJ:=omphalos
TAGS:=$(OUT)/tags
OMPHALOS:=$(OUT)/$(PROJ)/$(PROJ)
ADDCAPS:=tools/addcaps
SETUPCORE:=tools/setupcores

UI:=ncurses tty
BIN:=$(addprefix $(OMPHALOS)-,$(UI))

DFLAGS:=-D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE_EXTENDED -D_GNU_SOURCE
CFLAGS+=$(DFLAGS) -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -O2
DBCFLAGS+=$(DFLAGS) -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -g -ggdb
CFLAGS:=$(DBCFLAGS)
# FIXME can't use --default-symver with GNU gold
LFLAGS+=-Wl,-O2,--enable-new-dtags,--as-needed,--warn-common
LFLAGS+=-lpcap -lcap -lpciaccess -lsysfs
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
NCURSESOBJS:=$(filter $(OUT)/$(SRC)/ui/ncurses/%.o,$(COBJS))
TTYOBJS:=$(filter $(OUT)/$(SRC)/ui/tty/%.o,$(COBJS))

IANAOUI:=ieee-oui.txt

# Requires CAP_NET_ADMIN privileges bestowed upon the binary
livetest: sudobless $(IANAOUI)
	$(OMPHALOS)-ncurses -u ''

test: all $(TESTPCAPS) $(IANAOUI)
	for i in $(TESTPCAPS) ; do $(OMPHALOS)-tty -f $$i -u "" || exit 1 ; done

valgrind: all $(TESTPCAPS) $(IANAOUI)
	for i in $(TESTPCAPS) ; do valgrind --tool=memcheck --leak-check=full $(OMPHALOS)-tty -f $$i -u "" || exit 1 ; done

$(IANAOUI):
	get-oui -v -f $@

$(OMPHALOS)-ncurses: $(COREOBJS) $(NCURSESOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lpanel -lncursesw

$(OMPHALOS)-tty: $(COREOBJS) $(TTYOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lreadline

$(OUT)/%.o: %.c $(CINCS) $(MAKEFILE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# Should the network be inaccessible, and local copies are installed, try:
#DOC2MANXSL?=--nonet
$(OUT)/%.1omphalos: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2MANXSL) $<

$(TAGS): $(CINCS) $(CSRCS) $(wildcard $(SRC)/ui/*/*.c)
	@mkdir -p $(@D)
	$(CTAGS) -o $@ -R $(SRC)

clean:
	rm -rf $(OUT) core

clobber: clean
	rm -rf $(IANAOUI)

bless: all
	$(ADDCAPS) $(BIN)

sudobless: all $(ADDCAPS) $(SETUPCORE)
	sudo $(ADDCAPS) $(BIN)
	$(SETUPCORE)

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
