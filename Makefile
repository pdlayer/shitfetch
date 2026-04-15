include config.mk

TARGET = shitfetch
BUILD_DIR = build
OBJ = \
	$(BUILD_DIR)/sf.o \
	$(BUILD_DIR)/sfdetect.o \
	$(BUILD_DIR)/sflogo.o \
	$(BUILD_DIR)/sfrender.o \
	$(BUILD_DIR)/sfutil.o \
	$(BUILD_DIR)/sfansi.o \
	$(BUILD_DIR)/sfcolor.o

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

$(BUILD_DIR)/sflogo.o: sflogo.c sf.h sfascii.h sfansi.h sfcolor.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
