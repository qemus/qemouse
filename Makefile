CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
VERSION ?= 0.1.0

UDFREAD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libudfread 2>/dev/null)
UDFREAD_LIBS := $(shell $(PKG_CONFIG) --libs libudfread 2>/dev/null)

ifeq ($(strip $(UDFREAD_LIBS)),)
ifneq ($(filter clean uninstall,$(MAKECMDGOALS)),)
else
$(error libudfread development files not found; install libudfread-dev and pkg-config)
endif
endif

CPPFLAGS += $(UDFREAD_CFLAGS) -DUDFREAD_VERSION=\"$(VERSION)\"
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-D_FILE_OFFSET_BITS=64
LDLIBS += $(UDFREAD_LIBS)

TARGET := udfread
SOURCE := src/udfread.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: $(TARGET)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET)
