version = 3.7.0
include config.mk

all: version.h src.subdir-all

version.h: Makefile $(wildcard .git/index)
	@echo "create $@"
	$(Q)v="$$(git describe --tags 2>/dev/null)"; \
	echo "#define VERSION \"$${v:-$(version)}\"" > $@

options:
	@echo "vimb build options:"
	@echo "LIBS      = $(LIBS)"
	@echo "CFLAGS    = $(CFLAGS)"
	@echo "LDFLAGS   = $(LDFLAGS)"
	@echo "EXTCFLAGS = $(EXTCFLAGS)"
	@echo "CC        = $(CC)"

install: all
	@# binary
	install -d $(BINPREFIX)
	install -m 755 src/vimb $(BINPREFIX)/vimb
	@# extension
	install -d $(LIBDIR)
	install -m 644 src/webextension/$(EXTTARGET) $(LIBDIR)/$(EXTTARGET)
	@# man page
	install -d $(MANPREFIX)/man1
	@sed -e "s!VERSION!$(version)!g" \
		-e "s!PREFIX!$(PREFIX)!g" \
		-e "s!DATE!`date -u -r $(DOCDIR)/vimb.1 +'%m %Y' 2>/dev/null || date +'%m %Y'`!g" $(DOCDIR)/vimb.1 > $(MANPREFIX)/man1/vimb.1
	@# .desktop file
	install -d $(DOTDESKTOPPREFIX)
	install -m 644 vimb.desktop $(DOTDESKTOPPREFIX)/vimb.desktop
	@# .metainfo.xml file
	install -d $(METAINFOPREFIX)
	install -m 644 vimb.metainfo.xml $(METAINFOPREFIX)/vimb.metainfo.xml

uninstall:
	$(RM) $(BINPREFIX)/vimb
	$(RM) $(DESTDIR)$(MANDIR)/man1/vimb.1
	$(RM) $(LIBDIR)/$(EXTTARGET)
	$(RM) $(DOTDESKTOPPREFIX)/vimb.desktop
	$(RM) $(METAINFOPREFIX)/vimb.metainfo.xml

clean: src.subdir-clean test-clean

sandbox:
	$(Q)$(MAKE) clean
	$(Q)$(MAKE) RUNPREFIX=$(CURDIR)/sandbox/usr PREFIX=/usr EXTENSIONDIR=$(CURDIR)/sandbox/usr/lib/vimb DESTDIR=./sandbox install

runsandbox: sandbox
	sandbox/usr/bin/vimb

test: version.h
	$(MAKE) -C src vimb.so
	$(MAKE) -C tests

test-clean:
	$(MAKE) -C tests clean

%.subdir-all:
	$(Q)$(MAKE) -C $*

%.subdir-clean:
	$(Q)$(MAKE) -C $* clean

# --- zentoo-flavored install targets ---
# The upstream `install` target above gives you a stock vimb. These extra
# targets install the zentoo opinions on top: a baseline config with
# `dark-mode=on` + the `zm` keybind, and optionally the full Dark Reader
# theming stack + weekly fixes-DB refresh cron.
#
# Composition:
#   sudo make install                # stock vimb
#   sudo make install-config         # + dark-mode=on + zm binding
#   sudo make install-dark-reader    # + Dark Reader + weekly cron
#
# install-dark-reader fetches Dark Reader $(DARKREADER_VER) from npm.
# For offline builds (distro packaging), pre-download and pass:
#   sudo make install-dark-reader DR_TARBALL=/path/to/darkreader-4.9.128.tgz

SYSCONFDIR      ?= /etc
LIBEXECDIR      ?= $(DESTDIR)$(PREFIX)/libexec
DATAROOTDIR     ?= $(DESTDIR)$(PREFIX)/share
STATEDIR        ?= $(DESTDIR)/var/lib/vimb
DARKREADER_VER  ?= 4.9.128
DR_TARBALL      ?=

install-config:
	install -d $(DESTDIR)$(SYSCONFDIR)/vimb
	install -m 644 resources/etc-vimb-config $(DESTDIR)$(SYSCONFDIR)/vimb/config

install-dark-reader: install-config
	@dr_workdir="$$(mktemp -d)"; \
	trap "rm -rf $$dr_workdir" EXIT INT TERM; \
	if [ -n "$(DR_TARBALL)" ]; then \
	    cp "$(DR_TARBALL)" "$$dr_workdir/dr.tgz"; \
	else \
	    echo "Fetching Dark Reader $(DARKREADER_VER) from npm..."; \
	    curl -sSfL "https://registry.npmjs.org/darkreader/-/darkreader-$(DARKREADER_VER).tgz" \
	         -o "$$dr_workdir/dr.tgz"; \
	fi; \
	tar -xzf "$$dr_workdir/dr.tgz" -C "$$dr_workdir"; \
	install -d $(DATAROOTDIR)/vimb $(DATAROOTDIR)/vimb-dr-fixes \
	           $(LIBEXECDIR) $(DESTDIR)$(SYSCONFDIR)/cron.weekly $(STATEDIR); \
	install -m 644 "$$dr_workdir/package/darkreader.js" \
	    $(DATAROOTDIR)/vimb/darkreader.js; \
	cat "$$dr_workdir/package/darkreader.js" resources/scripts-bootstrap.js \
	    > $(DATAROOTDIR)/vimb/scripts.js; \
	chmod 644 $(DATAROOTDIR)/vimb/scripts.js; \
	install -m 755 resources/dr-fixes/refresh \
	    $(LIBEXECDIR)/vimb-dr-fixes-refresh; \
	install -m 644 resources/dr-fixes/bootstrap.js \
	    $(DATAROOTDIR)/vimb-dr-fixes/bootstrap.js; \
	install -m 755 resources/dr-fixes/weekly-cron \
	    $(DESTDIR)$(SYSCONFDIR)/cron.weekly/vimb-dr-fixes
	@echo ""
	@echo "Dark Reader stack installed."
	@echo "Populate /var/lib/vimb/scripts.js by running once:"
	@echo "    sudo $(PREFIX)/libexec/vimb-dr-fixes-refresh"

uninstall-config:
	$(RM) $(DESTDIR)$(SYSCONFDIR)/vimb/config
	@rmdir --ignore-fail-on-non-empty $(DESTDIR)$(SYSCONFDIR)/vimb 2>/dev/null || true

uninstall-dark-reader: uninstall-config
	$(RM) $(DATAROOTDIR)/vimb/darkreader.js
	$(RM) $(DATAROOTDIR)/vimb/scripts.js
	$(RM) $(DATAROOTDIR)/vimb-dr-fixes/bootstrap.js
	$(RM) $(LIBEXECDIR)/vimb-dr-fixes-refresh
	$(RM) $(DESTDIR)$(SYSCONFDIR)/cron.weekly/vimb-dr-fixes
	$(RM) $(STATEDIR)/scripts.js
	@rmdir --ignore-fail-on-non-empty $(DATAROOTDIR)/vimb-dr-fixes 2>/dev/null || true
	@rmdir --ignore-fail-on-non-empty $(STATEDIR) 2>/dev/null || true

.PHONY: all options install uninstall clean sandbox runsandbox install-config install-dark-reader uninstall-config uninstall-dark-reader
