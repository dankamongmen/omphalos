.DELETE_ON_ERROR:
#.DEFAULT_GOAL:=test
# all, [un]install, and *clean-local are autotoolizd
.PHONY: bin doc livetest coretest silenttest test valgrind
.PHONY:	bless sudobless postinstall postuninstall check

OUT:=out
SRC=src
DOC:=doc
PROJ:=omphalos
OMPHALOS:=$(OUT)/$(PROJ)/$(PROJ)
ADDCAPS:=tools/addcaps
SETUPCORE:=tools/setupcores

UI:=coretest @CONFIGURED_UIS@
BIN:=$(addsuffix $(EXEEXT),$(addprefix $(OMPHALOS)-,$(UI)))

DFLAGS=-D_XOPEN_SOURCE_EXTENDED -include config.h #-finput-charset=UTF-8
DFLAGS+=-D_FORTIFY_SOURCE=2
AM_CPPFLAGS:=-DOMPHALOS_DATADIR=\"$(datadir)\"
CFLAGS=$(DFLAGS) -g -ggdb -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Wno-format-zero-length @CFLAGS@ $(AM_CPPFLAGS)
DBCFLAGS:=$(DFLAGS) -march=native -pthread -I$(SRC) -fpic -fstrict-aliasing -fvisibility=hidden -Wall -W -Wextra -Wno-format-zero-length -g -ggdb @CFLAGS@ $(AM_CPPFLAGS)
AM_CFLAGS:=$(CFLAGS)
# FIXME can't use --default-symver with GNU gold
LFLAGS:=-Wl,-O2,--enable-new-dtags,--as-needed,--warn-common $(LIBS)

MANDIR:=$(DOC)/man
XMLDIR:=$(DOC)/xml
XHTMLDIR:=$(DOC)/xhtml
MAN1SRC:=$(wildcard $(MANDIR)/man1/*)
MAN1OBJ:=$(addsuffix .1,$(addprefix $(OUT)/$(MANDIR)/man1/$(PROJ)-,$(UI)))
XHTML:=$(addsuffix .xhtml,$(addprefix $(OUT)/$(XHTMLDIR)/$(PROJ)-,$(UI)))
DOCS:=$(MAN1OBJ) $(XHTML)

# This can be a URL; it's the docbook-to-manpage XSL
# Should the network be inaccessible, and local copies are installed, try:
#DOCMANXSL:=--nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/manpages/docbook.xsl
#DOCXHTMLXSL:=--nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/xhtml/docbook.xsl
DOCMANXSL:=http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl
DOCXHTMLXSL:=http://docbook.sourceforge.net/release/xsl/current/xhtml/docbook.xsl

IANAOUI:=ieee-oui.txt
SUPPORT:=$(IANAOUI)

all: bin doc #$(SUPPORT)

check: test

#ctags:
#	ctags --languages=C,Sh,Make -R .

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
NOTCURSESOBJS:=$(filter $(OUT)/$(SRC)/ui/notcurses/%.o,$(COBJS))
TTYOBJS:=$(filter $(OUT)/$(SRC)/ui/tty/%.o,$(COBJS))
XOBJS:=$(filter $(OUT)/$(SRC)/ui/x/%.o,$(COBJS))

# Requires CAP_NET_ADMIN privileges bestowed upon the binary
livetest: sudobless $(SUPPORT)
	$(OMPHALOS)-notcurses -u '' --plog=$(OUTCAP) --ouis=$(IANAOUI)

silenttest: sudobless $(SUPPORT)
	$(OMPHALOS)-notcurses -u '' --mode=silent --plog=$(OUTCAP) --ouis=$(IANAOUI)

coretest: sudobless $(SUPPORT)
	$(OMPHALOS)-coretest -u '' --plog=$(OUTCAP) --ouis=$(IANAOUI)

test: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do $(OMPHALOS)-tty --mode=silent --plog=$(OUTCAP) -f $$i -u "" --ouis=$(IANAOUI) || exit 1 ; done

valgrind: all $(TESTPCAPS) $(SUPPORT)
	for i in $(TESTPCAPS) ; do valgrind --tool=memcheck --leak-check=full $(OMPHALOS)-tty -f $$i -u "" --ouis=$(IANAOUI) || exit 1 ; done

# IEEE recently added a wholly useless second column consisting of a string in
# parens. Filter this column out, but don't touch further parens (no /g). 
$(IANAOUI):
	wget http://standards.ieee.org/develop/regauth/oui/oui.txt -O - \
	 | sed -e 's/([^)]*)//' > $@

$(OMPHALOS)-coretest: $(COREOBJS) $(CORETESTOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

$(OMPHALOS)-notcurses: $(COREOBJS) $(NOTCURSESOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS) $(NOTCURSES_LIBS)

$(OMPHALOS)-tty: $(COREOBJS) $(TTYOBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)

$(OUT)/%.o: %.c $(CINCS) $(MAKEFILE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT)/%.xhtml: $(OUT)/$(XHTMLDIR)/$(PROJ).xhtml
	@mkdir -p $(@D)
	ln -fs $(<F) $@

$(OUT)/%.1: $(OUT)/$(MANDIR)/man1/$(PROJ).1
	@mkdir -p $(@D)
	ln -fs $(<F) $@

$(OUT)/$(MANDIR)/man1/$(PROJ).1: $(MANDIR)/man1/$(PROJ).xml $(OUT)/$(XMLDIR)/$(PROJ).ent
	@mkdir -p $(@D)
	$(XSLTPROC) --path $(OUT)/$(XMLDIR) --writesubtree $(@D) -o $@ $(DOCMANXSL) $<

$(OUT)/$(XHTMLDIR)/$(PROJ).xhtml: $(MANDIR)/man1/$(PROJ).xml $(OUT)/$(XMLDIR)/$(PROJ).ent
	@mkdir -p $(@D)
	$(XSLTPROC) --path $(OUT)/$(XMLDIR) --writesubtree $(@D) -o $@ $(DOCXHTMLXSL) $<

$(OUT)/$(XMLDIR)/$(PROJ).ent:
	@mkdir -p $(@D)
	echo -e '<!ENTITY dhfirstname "Nick">\n<!ENTITY dhsurname "Black">\n<!ENTITY dhemail "dankamongmen@gmail.com">\n<!ENTITY dhusername "&dhfirstname; &dhsurname;">\n<!ENTITY dhrelease "$(VERSION)">' > "$@"

clean-local:
	rm -rf $(OUT) $(wildcard core*) $(wildcard vgcore*) ctags

maintainer-clean-local:
	rm -rf aclocal.m4 configure config config.in config.in~ \
		install-sh missing Makefile.in
	rm -rf $(SUPPORT)

bless: all
	$(ADDCAPS) $(BIN)

sudobless: all $(ADDCAPS) $(SETUPCORE)
	sudo $(ADDCAPS) $(BIN)
	$(SETUPCORE)

install-exec-local: all doc
	@mkdir -p $(DESTDIR)${bindir}
	$(ADDCAPS) $(BIN)
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)${bindir}

install-data-local: all doc
	@mkdir -p $(DESTDIR)${mandir}/man1 $(DESTDIR)${docdir}
	$(INSTALL_DATA) -m 0644 $(MAN1OBJ) $(DESTDIR)${mandir}/man1
	$(INSTALL_DATA) -m 0644 $(XHTML) $(DESTDIR)${docdir}/

postinstall: install
	@echo "Running $(MANBIN) ${mandir}..." && $(MANBIN) $(DESTDIR)${mandir}

postuninstall:
	@echo "Running $(MANBIN) ${mandir}..." && $(MANBIN) ${mandir}

uninstall-local:
	rm -f $(addprefix ${bindir}/,$(notdir $(BIN)))
	rm -f $(addprefix ${mandir}/man1/,$(notdir $(MAN1OBJ)))
	rm -f $(addprefix $(DESTDIR)${docdir}/,$(notdir $(XHTML)))

EXTRA_DIST=$(CSRCS) $(CINCS) ieee-oui.txt $(MAN1SRC) $(ADDCAPS) \
	   $(SETUPCORE) $(TESTPCAPS)
