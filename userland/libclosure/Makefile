# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libclosure
TAG     := libclosure-96
include ../common.mk

SOURCES := runtime.cpp generic_helpers.c data.c
OBJS    := $(patsubst %,$(OBJ)/%.o,$(basename $(SOURCES)))
DYLIB   := $(OBJ)/libsystem_blocks.dylib

FLAGS := $(BASE_CFLAGS) -fexceptions -DHAVE_OBJC=0 -DHAVE_UNWIND=1 -I$(SRCROOT)

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	mkdir -p $(OBJ)
	$(CC) $(FLAGS) -std=gnu11 -c $< -o $@
$(OBJ)/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched
	mkdir -p $(OBJ)
	$(CXX) $(FLAGS) -std=gnu++17 -fno-rtti -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_blocks.dylib \
		-compatibility_version 1 -current_version 96 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -lsystem_platform -lsystem_malloc \
		-Wl,-upward-lsystem_c -ldyld -lunwind -lcompiler_rt -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

installhdrs: $(SRCROOT)/.fetched
	$(call copy_headers,$(SRCROOT)/Block.h,usr/include)
	$(call copy_headers,$(SRCROOT)/Block_private.h,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
