CC = gcc

CFLAGS = `pkg-config --cflags gio-2.0 wayland-client xkbcommon libudev` -Iinclude -Iprotocol
LDFLAGS = `pkg-config --libs gio-2.0 wayland-client xkbcommon libudev` -lxdo
PROTO_DIR = protocol

PROTO_XML = $(PROTO_DIR)/virtual-keyboard-unstable-v1.xml \
            $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1.xml

PROTO_GEN = $(PROTO_DIR)/virtual-keyboard-unstable-v1-client-protocol.h \
            $(PROTO_DIR)/virtual-keyboard-unstable-v1-protocol.c \
            $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1-client-protocol.h \
            $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1-protocol.c

SOURCES = src/main.c \
          src/xdo_simulate.c \
          src/wayland_vinput.c \
          src/input_manager.c \
          src/settings.c \
          src/dbus.c \
          src/udev.c \
          $(PROTO_DIR)/virtual-keyboard-unstable-v1-protocol.c \
          $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1-protocol.c

WAYLAND_SCANNER ?= wayland-scanner

PREFIX ?= /usr

TARGET = input-redirector

all: $(TARGET)

$(TARGET): $(PROTO_GEN) $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

$(PROTO_DIR)/virtual-keyboard-unstable-v1-client-protocol.h: $(PROTO_DIR)/virtual-keyboard-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@

$(PROTO_DIR)/virtual-keyboard-unstable-v1-protocol.c: $(PROTO_DIR)/virtual-keyboard-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@

$(PROTO_DIR)/wlr-virtual-pointer-unstable-v1-client-protocol.h: $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@

$(PROTO_DIR)/wlr-virtual-pointer-unstable-v1-protocol.c: $(PROTO_DIR)/wlr-virtual-pointer-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@

clean:
	rm -f $(TARGET)
	rm -f $(PROTO_GEN)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/libexec
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/libexec/$(TARGET)
	install -d $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas
	install -m 0644 data/io.furios.input-redirector.gschema.xml $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas/
	install -d $(DESTDIR)$(PREFIX)/lib/systemd/user
	install -m 0644 data/input-redirector.service $(DESTDIR)$(PREFIX)/lib/systemd/user/
	glib-compile-schemas $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/libexec/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas/io.furios.input-redirector.gschema.xml
	rm -f $(DESTDIR)$(PREFIX)/lib/systemd/user/input-redirector.service
