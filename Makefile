include config.mk

BUILD_DIR = build
TARGET_NAME = shitfetch
TARGET = $(BUILD_DIR)/$(TARGET_NAME)
SHITFETCH_VERSION = $(shell sed -n 's/^#define SHITFETCH_VERSION "\([^"]*\)"/\1/p' sf.h)
DEB_RELEASE ?= 1
DEB_VERSION ?= $(SHITFETCH_VERSION)-$(DEB_RELEASE)
DEB_ARCH ?= $(shell dpkg --print-architecture)
DEB_ROOT = $(BUILD_DIR)/deb/$(TARGET_NAME)
DEB = $(TARGET_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb
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
	$(BUILD_DIR)/sfcolor.o

CPPFLAGS += -DSHITFETCH_ASCII_DIR='"/usr/local/share/shitfetch/ascii"'
CFLAGS += -std=$(CSTD) -Wall -Wextra -Wpedantic -Wno-trigraphs -Wno-format-truncation

.PHONY: all clean deb install uninstall

all: $(TARGET)

$(TARGET): $(OBJ) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c sf.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sflogo.o: sflogo.c sf.h sfascii.h sfminiascii.h sfansi.h sfcolor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

deb: $(TARGET)
	rm -rf $(DEB_ROOT)
	install -d $(DEB_ROOT)/DEBIAN
	install -d $(DEB_ROOT)/usr/bin
	install -d $(DEB_ROOT)/usr/share/man/man1
	install -d $(DEB_ROOT)/usr/share/man/man5
	install -d $(DEB_ROOT)/usr/share/man/man7
	install -d $(DEB_ROOT)/usr/share/bash-completion/completions
	install -d $(DEB_ROOT)/usr/share/fish/vendor_completions.d
	install -d $(DEB_ROOT)/usr/share/zsh/site-functions
	install -d $(DEB_ROOT)/usr/share/licenses/$(TARGET_NAME)
	install -m 755 $(TARGET) $(DEB_ROOT)/usr/bin/$(TARGET_NAME)
	gzip -cn $(MAN1) > $(DEB_ROOT)/usr/share/man/man1/$(MAN1).gz
	gzip -cn $(MAN5) > $(DEB_ROOT)/usr/share/man/man5/$(MAN5).gz
	gzip -cn $(MAN7) > $(DEB_ROOT)/usr/share/man/man7/$(MAN7).gz
	install -m 644 shitfetch.bash $(DEB_ROOT)/usr/share/bash-completion/completions/$(TARGET_NAME)
	install -m 644 shitfetch.fish $(DEB_ROOT)/usr/share/fish/vendor_completions.d/$(TARGET_NAME).fish
	install -m 644 shitfetch.zsh $(DEB_ROOT)/usr/share/zsh/site-functions/_$(TARGET_NAME)
	install -m 644 LICENSE $(DEB_ROOT)/usr/share/licenses/$(TARGET_NAME)/LICENSE
	printf '%s\n' \
		'Package: $(TARGET_NAME)' \
		'Version: $(DEB_VERSION)' \
		'Section: utils' \
		'Priority: optional' \
		'Architecture: $(DEB_ARCH)' \
		'Maintainer: pdlayer' \
		'Depends: libc6, libdrm2' \
		'Description: minimal linux fetch' \
		> $(DEB_ROOT)/DEBIAN/control
	dpkg-deb --build --root-owner-group $(DEB_ROOT) $(DEB)

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
