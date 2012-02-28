.DELETE_ON_ERROR:
#.DEFAULT_GOAL:=test
.PHONY: all bin doc livetest coretest silenttest test valgrind clean clobber install uninstall
.PHONY:	bless sudobless

OUT:=out
SRC=src
DOC:=doc
PROJ:=omphalos
OMPHALOS:=$(OUT)/$(PROJ)/$(PROJ)
ADDCAPS:=tools/addcaps
SETUPCORE:=tools/setupcores

UI:=coretest @CONFIGURED_UIS@
BIN:=$(addprefix $(OMPHALOS)-,$(UI))

DFLAGS=-D_XOPEN_SOURCE_EXTENDED --include config.h
CFLAGS=$(DFLAGS) -O2 -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -Wno-format-zero-length @CFLAGS@
DBCFLAGS:=$(DFLAGS) -O2 -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Werror -Wno-format-zero-length -g -ggdb @CFLAGS@
AM_CFLAGS:=$(DBCFLAGS)
# FIXME can't use --default-symver with GNU gold
LFLAGS:=-Wl,-O2,--enable-new-dtags,--as-needed,--warn-common $(LIBS)

MANDIR:=doc/man
XHTMLDIR:=doc/xhtml
MAN1SRC:=$(wildcard $(MANDIR)/man1/*)
MAN1:=$(addprefix $(OUT)/,$(MAN1SRC:%.xml=%.1$(PROJ)))
MAN1OBJ:=$(addprefix $(OUT)/,$(MAN1SRC:%.xml=%.1))
XHTML:=$(addprefix $(OUT)/$(XHTMLDIR)/,$(notdir $(MAN1SRC:%.xml=%.xhtml)))
DOCS:=$(MAN1OBJ) $(XHTML)

# This can be a URL; it's the docbook-to-manpage XSL
# Should the network be inaccessible, and local copies are installed, try:
#DOC2MANXSL?=--nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/manpages/docb
#DOC2XHTMLXSL?=http://docbook.sourceforge.net/release/xsl/current/xhtml/docbook.xsl

all: tags bin doc

tags:
	ctags -R .

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
CORETESTOBJS:=$(filter $(OUT)/$(SRC)/ui/coretest/%.o,$(COBJS))
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

coretest: sudobless $(SUPPORT)
	$(OMPHALOS)-coretest -u '' --plog=$(OUTCAP)

test: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do $(OMPHALOS)-tty --mode=silent --plog=$(OUTCAP) -f $$i -u "" || exit 1 ; done

valgrind: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do valgrind --tool=memcheck --leak-check=full $(OMPHALOS)-tty -f $$i -u "" || exit 1 ; done

# Even with --header='Accept-Charset: utf-8', we get served up ISO-8859-1, yuck
$(USBIDS):
	wget http://www.linux-usb.org/usb.ids -O - | iconv -f iso-8859-1 -t utf-8 -o $@

$(IANAOUI): $(IANAOUI).raw
	iconv -f iso-8859-1 -t utf-8 -o $@ $<

$(IANAOUI).raw:
	get-oui -v -f $@

$(OMPHALOS)-coretest: $(COREOBJS) $(CORETESTOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

$(OMPHALOS)-ncurses: $(COREOBJS) $(NCURSESOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(PANEL_LIB) $(CURSES_LIB)

$(OMPHALOS)-tty: $(COREOBJS) $(TTYOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

$(OMPHALOS)-x: $(COREOBJS) $(XOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(X11_CFLAGS) -o $@ $^ $(LFLAGS) $(X11_LIBS)

$(OUT)/%.o: %.c $(CINCS) $(MAKEFILE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.1: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2MANXSL) $<

$(OUT)/%.xhtml: %.xml
	@mkdir -p $(@D)
	$(XSLTPROC) --writesubtree $(@D) -o $@ $(DOC2XHTMLXSL) $<

clean-local:
	rm -rf $(OUT) $(wildcard core*) $(wildcard vgcore*)

clobber: clean
	rm -rf $(IANAOUI) $(IANAOUI).raw $(USBIDS)

bless: all
	$(ADDCAPS) $(BIN)

sudobless: all $(ADDCAPS) $(SETUPCORE)
	sudo $(ADDCAPS) $(BIN)
	$(SETUPCORE)

install-exec-local: all doc
	@mkdir -p ${bindir}
	@$(INSTALL) -m 0755 $(BIN) ${bindir}

install-data-local: all doc
	@mkdir -p ${mandir}/man1
	@$(INSTALL_DATA) -m 0644 $(MAN1OBJ) ${mandir}/man1
	@echo "Running $(MANBIN) ${mandir}..." && $(MANBIN) ${mandir}

uninstall-local:
	rm -f $(addprefix ${bindir}/,$(notdir $(BIN)))
	rm -f $(addprefix ${mandir}/man1/,$(notdir $(MAN1OBJ)))
	@echo "Running $(MANBIN) ${mandir}..." && $(MANBIN) ${mandir}
