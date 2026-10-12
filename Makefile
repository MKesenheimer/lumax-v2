# Makefile for liblumax - Lumax laser controller library
# Builds shared library for Linux and macOS

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC
LDFLAGS ?= -shared

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    TARGET = liblumax.so
    LDFLAGS += -Wl,-soname,liblumax.so
    # FTDI library via pkg-config
    LDLIBS += $(shell pkg-config --libs libftdi1)
    # Adjust include path: pkg-config gives us -I/path/libftdi1 but we need -I/path
    FTDI_CFLAGS := $(shell pkg-config --cflags libftdi1)
    FTDI_INCLUDE_PATH := $(patsubst -I%/libftdi1, -I%, $(FTDI_CFLAGS))
    INCLUDES = -I. $(FTDI_INCLUDE_PATH)
endif
ifeq ($(UNAME_S),Darwin)
    TARGET = liblumax.dylib
    LDFLAGS += -Wl,-install_name,@rpath/liblumax.dylib
    # FTDI library via pkg-config
    LDLIBS += $(shell pkg-config --libs libftdi1)
    # Adjust include path: pkg-config gives us -I/path/libftdi1 but we need -I/path
    FTDI_CFLAGS := $(shell pkg-config --cflags libftdi1)
    FTDI_INCLUDE_PATH := $(patsubst -I%/libftdi1, -I%, $(FTDI_CFLAGS))
    INCLUDES = -I. $(FTDI_INCLUDE_PATH)
endif

# Source files
SRCS = liblumax.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean