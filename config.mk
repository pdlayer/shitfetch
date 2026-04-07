PREFIX ?= /usr/local

CC ?= cc
CSTD ?= c11

CPPFLAGS ?=
CFLAGS ?= -O2 -pipe
LDFLAGS ?=
LDLIBS ?= -lkdl -lm -ldrm
CFLAGS += $(shell pkg-config --cflags libdrm)
