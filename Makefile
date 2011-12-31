.DELTE_ON_ERROR:
.DEFAULT_GOAL:=test
.PHONY: all bin doc livetest silenttest test valgrind clean clobber install uninstall
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

UI:=ncurses tty# x
BIN:=$(addprefix $(OMPHALOS)-,$(UI))

DFLAGS:=-D_FILE_OFFSET_BITS=64 -D_XOPEN_SOURCE_EXTENDED -D_GNU_SOURCE
CFLAGS+=$(DFLAGS) -O2 -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -Wno-format-zero-length
DBCFLAGS+=$(DFLAGS) -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -Wno-format-zero-length -g -ggdb
CFLAGS:=$(DBCFLAGS)
# FIXME can't use --default-symver with GNU gold
LFLAGS+=-Wl,-O2,--enable-new-dtags,--as-needed,--warn-common
LFLAGS+=-liw -lpcap -lcap -lpciaccess $(shell  pkg-config --libs libnl-3.0) $(shell pkg-config --libs libsysfs) $(shell pkg-config --libs zlib)
CTAGS?=$(shell (which ctags || echo ctags) 2> /dev/null)
XSLTPROC?=$(shell (which xsltproc || echo xsltproc) 2> /dev/null)
INSTALL?=install -v
DESTDIR?=/usr/local
ifeq ($(UNAME),FreeBSD)
DOCPREFIX?=$(DESTDIR)/man
MANBIN?=makewhatis
else
DOCPREFIX?=$(DESTDIR)/share/man
MANBIN?=mandb
endif

MANDIR:=doc/man
XHTMLDIR:=doc/xhtml
MAN1SRC:=$(wildcard $(MANDIR)/man1/*)
MAN1:=$(addprefix $(OUT)/,$(MAN1SRC:%.xml=%.1$(PROJ)))
MAN1OBJ:=$(addprefix $(OUT)/,$(MAN1SRC:%.xml=%.1))
XHTML:=$(addprefix $(OUT)/$(XHTMLDIR)/,$(notdir $(MAN1SRC:%.xml=%.xhtml)))
DOCS:=$(MAN1OBJ) $(XHTML)

# Any old XSLT processor ought do, but you might need change the invocation.
XSLTPROC?=$(shell (which xsltproc || echo xsltproc) 2> /dev/null)
# This can be a URL; it's the docbook-to-manpage XSL
# Should the network be inaccessible, and local copies are installed, try:
#DOC2MANXSL?=--nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/manpages/docb
DOC2XHTMLXSL?=http://docbook.sourceforge.net/release/xsl/current/xhtml/docbook.xsl

all: $(TAGS) bin doc

bin: $(BIN)

doc: $(DOCS)

OUTCAP:=$(OUT)/plog.pcap
TESTPCAPS:=$(wildcard test/*)

CSRCDIRS:=$(wildcard $(SRC)/*)
CSRCS:=$(shell find $(CSRCDIRS) -type f -iname \*.c -print)
CINCS:=$(shell find $(CSRCDIRS) -type f -iname \*.h -print)
COBJS:=$(addprefix $(OUT)/,$(CSRCS:%.c=%.o))

# Various UI's plus the core make the binaries
COREOBJS:=$(filter $(OUT)/$(SRC)/$(PROJ)/%.o,$(COBJS))
NCURSESOBJS:=$(filter $(OUT)/$(SRC)/ui/ncurses/%.o,$(COBJS))
TTYOBJS:=$(filter $(OUT)/$(SRC)/ui/tty/%.o,$(COBJS))
XOBJS:=$(filter $(OUT)/$(SRC)/ui/x/%.o,$(COBJS))

USBIDS:=usb.ids
IANAOUI:=ieee-oui.txt
SUPPORT:=$(USBIDS) $(IANAOUI)

# Requires CAP_NET_ADMIN privileges bestowed upon the binary
livetest: sudobless $(SUPPORT)
	$(OMPHALOS)-ncurses -u '' --plog=$(OUTCAP)

silenttest: sudobless $(SUPPORT)
	$(OMPHALOS)-ncurses -u '' --mode=silent --plog=$(OUTCAP)

test: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do $(OMPHALOS)-tty --plog=$(OUTCAP) -f $$i -u "" || exit 1 ; done

valgrind: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do valgrind --tool=memcheck --leak-check=full $(OMPHALOS)-tty -f $$i -u "" || exit 1 ; done

# Even with --header='Accept-Charset: utf-8', we get served up ISO-8859-1, yuck
$(USBIDS):
	wget http://www.linux-usb.org/usb.ids -O - | iconv -f iso-8859-1 -t utf-8 -o $@

$(IANAOUI): $(IANAOUI).raw
	iconv -f iso-8859-1 -t utf-8 -o $@ $<

$(IANAOUI).raw:
	get-oui -v -f $@

$(OMPHALOS)-ncurses: $(COREOBJS) $(NCURSESOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lpanel -lncursesw

$(OMPHALOS)-tty: $(COREOBJS) $(TTYOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lreadline -lncursesw

$(OMPHALOS)-x: $(COREOBJS) $(XOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) -lX11

$(OUT)/%.o: %.c $(CINCS) $(MAKEFILE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.1: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2MANXSL) $<

$(OUT)/%.xhtml: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2XHTMLXSL) $<

$(TAGS): $(CINCS) $(CSRCS) $(wildcard $(SRC)/ui/*/*.c)
	@mkdir -p $(@D)
	$(CTAGS) -o $@ -R $(SRC)

clean:
	rm -rf $(OUT) $(wildcard core*) $(wildcard vgcore*)

clobber: clean
	rm -rf $(IANAOUI) $(IANAOUI).raw $(USBIDS)

bless: all
	$(ADDCAPS) $(BIN)

sudobless: all $(ADDCAPS) $(SETUPCORE)
	sudo $(ADDCAPS) $(BIN)
	$(SETUPCORE)

install: all doc
	@mkdir -p $(DESTDIR)/bin
	@$(INSTALL) -m 0755 $(BIN) $(DESTDIR)/bin
	@mkdir -p $(DOCPREFIX)/man1
	@$(INSTALL) -m 0644 $(MAN1OBJ) $(DOCPREFIX)/man1
	@echo "Running $(MANBIN) $(DOCPREFIX)..." && $(MANBIN) $(DOCPREFIX)

uninstall:
	rm -f $(addprefix $(DESTDIR)/bin/,$(notdir $(BIN)))
	rm -f $(addprefix $(DOCPREFIX)/man1/,$(notdir $(MAN1OBJ)))
	@echo "Running $(MANBIN) $(DOCPREFIX)..." && $(MANBIN) $(DOCPREFIX)
