# Makefile for test program
# Compiles main.c and links against liblumax from ../libs

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?=

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LIB_EXT = .so
    LDFLAGS += -Wl,-rpath,'$$ORIGIN/../libs'
endif
ifeq ($(UNAME_S),Darwin)
    LIB_EXT = .dylib
    LDFLAGS += -Wl,-rpath,@loader_path/../libs
endif

LIB_DIR = ../libs
LIB_NAME = liblumax$(LIB_EXT)
LIB_PATH = $(LIB_DIR)/$(LIB_NAME)

TARGET = test
SRCS = main.c

# Include path for headers
INCLUDES = -I..

all: $(TARGET)

$(TARGET): $(SRCS) $(LIB_PATH)
	$(CC) $(CFLAGS) $(INCLUDES) -L$(LIB_DIR) -llumax -o $@ $< $(LDFLAGS)

# Ensure library exists
$(LIB_PATH):
	@echo "Library $(LIB_PATH) not found. Building from parent directory..."
	$(MAKE) -C ..

clean:
	rm -f $(TARGET)

.PHONY: all clean