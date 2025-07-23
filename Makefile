CC = gcc

CFLAGS = `pkg-config --cflags gio-2.0` -Iinclude
LDFLAGS = `pkg-config --libs gio-2.0` -lxdo

SOURCES = src/main.c src/xdo_simulate.c src/input_manager.c src/settings.c
TARGET = input-redirector

PREFIX ?= /usr

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

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
