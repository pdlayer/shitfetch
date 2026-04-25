PREFIX ?= /usr/local

CC = clang
CSTD ?= c11

CPPFLAGS ?=
CFLAGS ?= -O2 -pipe
LDFLAGS ?= -static -s
CFLAGS += -pthread
LDLIBS = -lm $(shell pkg-config --libs --static libdrm) -pthread
CFLAGS += $(shell pkg-config --cflags libdrm)
