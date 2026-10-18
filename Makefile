# Makefile for liblumax - Lumax laser controller library
# Builds shared library for Linux and macOS, outputs to ./libs and ./python/libs

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

# Output directories
LIB_DIR = ./libs
PYTHON_LIB_DIR = ./python/libs

# Source files
SRCS = liblumax.c
OBJS = $(SRCS:.c=.o)

# Targets in lib directory
LIB_TARGET = $(LIB_DIR)/$(TARGET)
LIB_OBJ = $(LIB_DIR)/liblumax.o

# Targets in python lib directory
PYTHON_LIB_TARGET = $(PYTHON_LIB_DIR)/$(TARGET)
PYTHON_LIB_OBJ = $(PYTHON_LIB_DIR)/liblumax.o

all: $(LIB_TARGET) $(PYTHON_LIB_TARGET)

# Build library in ./libs
$(LIB_TARGET): $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(LIB_OBJ): liblumax.c
	@mkdir -p $(LIB_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build library in ./python/libs (copy from ./libs)
$(PYTHON_LIB_TARGET): $(LIB_TARGET)
	@mkdir -p $(PYTHON_LIB_DIR)
	cp $(LIB_TARGET) $(PYTHON_LIB_TARGET)
	cp $(LIB_OBJ) $(PYTHON_LIB_OBJ)

# Also copy .o file
$(PYTHON_LIB_OBJ): $(LIB_OBJ)
	@mkdir -p $(PYTHON_LIB_DIR)
	cp $(LIB_OBJ) $(PYTHON_LIB_OBJ)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf $(LIB_DIR) $(PYTHON_LIB_DIR)

.PHONY: all clean