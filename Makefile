SHELL := /bin/bash
.SHELLFLAGS := -Eeuo pipefail -c

CC ?= cc
GIT ?= git
MESON ?= meson
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
VERSION ?= 0.1.0

LIBUDFREAD_REPOSITORY ?= https://code.videolan.org/videolan/libudfread.git
LIBUDFREAD_REF ?=
LIBUDFREAD_SOURCE ?= vendor/libudfread
BUILD_DIR ?= build
LIBUDFREAD_BUILD := $(BUILD_DIR)/libudfread
LIBUDFREAD_PREFIX := $(abspath $(BUILD_DIR)/libudfread-install)
LIBUDFREAD_PKGCONFIG := $(LIBUDFREAD_PREFIX)/lib/pkgconfig
LIBUDFREAD_STAMP := $(LIBUDFREAD_PREFIX)/.built

CPPFLAGS += -DUDFREAD_VERSION=\"$(VERSION)\"
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-D_FILE_OFFSET_BITS=64 -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections

TARGET := udfread
SOURCE := src/udfread.c

.PHONY: all clean distclean install test uninstall vendor

all: $(TARGET)

test: $(TARGET)
	./tests/test.sh ./$(TARGET)

$(LIBUDFREAD_SOURCE)/meson.build:
	@set -Eeuo pipefail; \
	repository='$(LIBUDFREAD_REPOSITORY)'; \
	ref='$(LIBUDFREAD_REF)'; \
	if [ -z "$$ref" ]; then \
		refs=$$($(GIT) ls-remote \
			--tags \
			--refs \
			--sort='v:refname' \
			"$$repository"); \
		ref=$$(sed 's|.*/||' <<< "$$refs" | \
			grep -E '^v?[0-9]+(\.[0-9]+){1,2}$$' | \
			tail -n 1); \
	fi; \
	if [ -z "$$ref" ]; then \
		echo 'ERROR: No upstream libudfread release tag was found.' >&2; \
		exit 1; \
	fi; \
	echo "Cloning libudfread tag: $$ref"; \
	rm -rf '$(LIBUDFREAD_SOURCE)'; \
	mkdir -p '$(@D)'; \
	$(GIT) clone \
		--quiet \
		--depth 1 \
		--branch "$$ref" \
		"$$repository" \
		'$(LIBUDFREAD_SOURCE)'

vendor: $(LIBUDFREAD_SOURCE)/meson.build

$(LIBUDFREAD_STAMP): $(LIBUDFREAD_SOURCE)/meson.build Makefile
	rm -rf '$(LIBUDFREAD_BUILD)' '$(LIBUDFREAD_PREFIX)'
	$(MESON) setup \
		'$(LIBUDFREAD_BUILD)' \
		'$(LIBUDFREAD_SOURCE)' \
		--buildtype=release \
		--default-library=static \
		--prefix='$(LIBUDFREAD_PREFIX)' \
		--libdir=lib
	$(MESON) compile -C '$(LIBUDFREAD_BUILD)'
	$(MESON) install -C '$(LIBUDFREAD_BUILD)'
	@test -f '$(LIBUDFREAD_PREFIX)/lib/libudfread.a' || { \
		echo 'ERROR: The static libudfread archive was not created.' >&2; \
		exit 1; \
	}
	@touch '$@'

$(TARGET): $(SOURCE) $(LIBUDFREAD_STAMP)
	@set -Eeuo pipefail; \
	udfread_cflags=$$(PKG_CONFIG_PATH='$(LIBUDFREAD_PKGCONFIG)' \
		$(PKG_CONFIG) --cflags libudfread); \
	udfread_libs=$$(PKG_CONFIG_PATH='$(LIBUDFREAD_PKGCONFIG)' \
		$(PKG_CONFIG) --static --libs libudfread); \
	$(CC) $(CPPFLAGS) $$udfread_cflags $(CFLAGS) $(LDFLAGS) \
		-o '$@' '$(SOURCE)' $$udfread_libs
	@if ldd './$(TARGET)' 2>/dev/null | grep -q 'libudfread'; then \
		echo 'ERROR: udfread was dynamically linked to libudfread.' >&2; \
		exit 1; \
	fi

install: $(TARGET)
	install -d '$(DESTDIR)$(BINDIR)'
	install -m 0755 '$(TARGET)' '$(DESTDIR)$(BINDIR)/$(TARGET)'

uninstall:
	rm -f '$(DESTDIR)$(BINDIR)/$(TARGET)'

clean:
	rm -rf '$(TARGET)' '$(BUILD_DIR)'

distclean: clean
	rm -rf '$(LIBUDFREAD_SOURCE)'
