PREFIX ?= /usr/local

CC = clang
CSTD ?= c11

CPPFLAGS ?=
CFLAGS ?= -O2 -pipe
LDFLAGS ?= -s
ifeq ($(OS),Windows_NT)
LDLIBS = -lm -luser32 -liphlpapi -ladvapi32 -lws2_32
else
LDLIBS = -lm $(shell pkg-config --libs libdrm)
CFLAGS += $(shell pkg-config --cflags libdrm)
endif
