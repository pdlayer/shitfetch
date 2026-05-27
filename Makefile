include config.mk

BUILD_DIR = build
TARGET_NAME = shitfetch
TARGET = $(BUILD_DIR)/$(TARGET_NAME)
MAN1 = shitfetch.1
MAN5 = shitfetch.5
MAN7 = shitfetch.7
OBJ = \
	$(BUILD_DIR)/sf.o \
	$(BUILD_DIR)/sfdetect.o \
	$(BUILD_DIR)/sfdetectpkgs.o \
	$(BUILD_DIR)/sfdetectgpu.o \
	$(BUILD_DIR)/sfdetectdisk.o \
	$(BUILD_DIR)/sfdetectdisplay.o \
	$(BUILD_DIR)/sflogo.o \
	$(BUILD_DIR)/sfrender.o \
	$(BUILD_DIR)/sfconfig.o \
	$(BUILD_DIR)/sfutil.o \
	$(BUILD_DIR)/sfansi.o \
	$(BUILD_DIR)/sfcolor.o \
	$(BUILD_DIR)/sfminiansi.o

CPPFLAGS += -DSHITFETCH_ASCII_DIR='"/usr/local/share/shitfetch/ascii"'
CFLAGS += -std=$(CSTD) -Wall -Wextra -Wpedantic -Wno-trigraphs -Wno-format-truncation

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c sf.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sflogo.o: sflogo.c sf.h sfascii.h sfminiascii.h sfansi.h sfcolor.h sfminiansi.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -d $(DESTDIR)$(PREFIX)/share/man/man5
	install -d $(DESTDIR)$(PREFIX)/share/man/man7
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET_NAME)
	install -m 644 $(MAN1) $(DESTDIR)$(PREFIX)/share/man/man1/shitfetch.1
	install -m 644 $(MAN5) $(DESTDIR)$(PREFIX)/share/man/man5/shitfetch.5
	install -m 644 $(MAN7) $(DESTDIR)$(PREFIX)/share/man/man7/shitfetch.7

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET_NAME)
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/shitfetch.1
	rm -f $(DESTDIR)$(PREFIX)/share/man/man5/shitfetch.5
	rm -f $(DESTDIR)$(PREFIX)/share/man/man7/shitfetch.7
