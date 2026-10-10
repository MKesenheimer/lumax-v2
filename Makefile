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
    # FTDI library
    LDLIBS += -lftdi1
    INCLUDES = -I. -I/opt/homebrew/include
endif
ifeq ($(UNAME_S),Darwin)
    TARGET = liblumax.dylib
    LDFLAGS += -Wl,-install_name,@rpath/liblumax.dylib
    # FTDI library
    LDLIBS += -lftdi1 -L/opt/homebrew/lib
    INCLUDES = -I. -I/opt/homebrew/include
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