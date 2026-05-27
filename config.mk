PREFIX ?= /usr/local

CC = clang
CSTD ?= c11

CPPFLAGS ?=
CFLAGS ?= -O2 -pipe
LDFLAGS ?= -s
LDLIBS = -lm $(shell pkg-config --libs libdrm)
CFLAGS += $(shell pkg-config --cflags libdrm)
