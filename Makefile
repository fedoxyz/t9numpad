# t9numpad Makefile
# Targets: all, install, uninstall, clean, dist, check

PACKAGE  := t9numpad
VERSION  := 0.1.0

# ── Directories ─────────────────────────────────────────────────────────────
PREFIX   ?= /usr
BINDIR   ?= $(PREFIX)/bin
LIBDIR   ?= $(PREFIX)/lib
MANDIR   ?= $(PREFIX)/share/man
DATADIR  ?= $(PREFIX)/share/$(PACKAGE)
SYSCFGDIR?= /etc/$(PACKAGE)
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system
UDEV_RULES_DIR   ?= /etc/udev/rules.d
VARDIR   ?= /var/lib/$(PACKAGE)

SRCDIR   := src
INCDIR   := include
BUILDDIR := build

# ── Compiler ─────────────────────────────────────────────────────────────────
CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow \
           -D_GNU_SOURCE \
           -DT9NUMPAD_VERSION=\"$(VERSION)\" \
           -I$(INCDIR)
LDFLAGS +=

ifeq ($(DEBUG), 1)
  CFLAGS += -O0 -g3 -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
else
  CFLAGS += -O2 -DNDEBUG
endif

# ── Sources — main daemon ─────────────────────────────────────────────────────
SRCS := $(SRCDIR)/main.c \
        $(SRCDIR)/t9.c \
        $(SRCDIR)/uinput.c \
        $(SRCDIR)/config.c \
        $(SRCDIR)/log.c

OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET := $(BUILDDIR)/$(PACKAGE)

# ── Sources — t9preview overlay ───────────────────────────────────────────────
PREVIEW_SRC    := $(SRCDIR)/t9preview.c
PREVIEW_TARGET := $(BUILDDIR)/t9preview
# t9preview only needs X11; use a separate flags set so X11 doesn't leak
# into the daemon build.
PREVIEW_CFLAGS  := -std=c11 -Wall -Wextra -O2 -DNDEBUG -D_POSIX_C_SOURCE=200809L
PREVIEW_LDFLAGS := -lX11

# ── Default target ────────────────────────────────────────────────────────────
.PHONY: all
all: $(TARGET) $(PREVIEW_TARGET)

$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $@

-include $(DEPS)

# t9preview is a single-file build — no need to involve the daemon objects
$(PREVIEW_TARGET): $(PREVIEW_SRC) | $(BUILDDIR)
	$(CC) $(PREVIEW_CFLAGS) -o $@ $< $(PREVIEW_LDFLAGS)

# ── Install ───────────────────────────────────────────────────────────────────
.PHONY: install
install: all install-bin install-preview install-service install-udev \
         install-man install-data install-config

install-bin:
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(PACKAGE)

install-preview:
	install -Dm755 $(PREVIEW_TARGET) $(DESTDIR)$(BINDIR)/t9preview

install-service:
	install -Dm644 systemd/$(PACKAGE).service \
	    $(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(PACKAGE).service

install-udev:
	install -Dm644 99-uinput.rules \
	    $(DESTDIR)$(UDEV_RULES_DIR)/99-uinput.rules

install-man:
	install -Dm644 man/$(PACKAGE).1 \
	    $(DESTDIR)$(MANDIR)/man1/$(PACKAGE).1
	gzip -f $(DESTDIR)$(MANDIR)/man1/$(PACKAGE).1

install-data:
	install -dm755 $(DESTDIR)$(DATADIR)
	[ -d data ] && install -Dm644 data/* $(DESTDIR)$(DATADIR)/ || true

install-config:
	install -Dm644 $(PACKAGE).toml.example \
	    $(DESTDIR)$(SYSCFGDIR)/$(PACKAGE).toml
	install -dm755 $(DESTDIR)$(VARDIR)

# ── Uninstall ─────────────────────────────────────────────────────────────────
.PHONY: uninstall
uninstall:
	rm -f  $(DESTDIR)$(BINDIR)/$(PACKAGE)
	rm -f  $(DESTDIR)$(BINDIR)/t9preview
	rm -f  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(PACKAGE).service
	rm -f  $(DESTDIR)$(UDEV_RULES_DIR)/99-uinput.rules
	rm -f  $(DESTDIR)$(MANDIR)/man1/$(PACKAGE).1.gz
	rm -rf $(DESTDIR)$(DATADIR)

# ── Distribution tarball ──────────────────────────────────────────────────────
DISTNAME := $(PACKAGE)-$(VERSION)

.PHONY: dist
dist:
	git archive --format=tar.gz --prefix=$(DISTNAME)/ \
	    -o $(DISTNAME).tar.gz HEAD
	sha256sum $(DISTNAME).tar.gz > $(DISTNAME).tar.gz.sha256
	@echo "Created $(DISTNAME).tar.gz"

# ── Tests ─────────────────────────────────────────────────────────────────────
.PHONY: check
check:
	@if [ -d tests ]; then \
	    $(MAKE) -C tests; \
	else \
	    echo "No tests directory found"; \
	fi

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(BUILDDIR)

.PHONY: distclean
distclean: clean
	rm -f $(DISTNAME).tar.gz $(DISTNAME).tar.gz.sha256

# ── Help ──────────────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "Targets:"
	@echo "  all         Build $(PACKAGE) and t9preview (default)"
	@echo "  install     Install all files under PREFIX=$(PREFIX)"
	@echo "  uninstall   Remove installed files"
	@echo "  check       Run tests"
	@echo "  dist        Create source tarball for AUR"
	@echo "  clean       Remove build artefacts"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX=$(PREFIX)   DESTDIR=   DEBUG=0|1"
