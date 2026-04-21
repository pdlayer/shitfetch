PREFIX ?= /usr/local

CC ?= cc
CSTD ?= c11

CPPFLAGS ?=
CFLAGS ?= -O2 -pipe
LDFLAGS ?=
CFLAGS += -pthread
LDLIBS ?= -lm -ldrm -pthread
CFLAGS += $(shell pkg-config --cflags libdrm)
