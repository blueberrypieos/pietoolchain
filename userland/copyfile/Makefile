# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := copyfile
TAG     := copyfile-240
include ../common.mk

SOURCES := copyfile.c xattr_flags.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libcopyfile.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu11 -fno-common -Wall -D__DARWIN_NOW_CANCELABLE=1 \
	-ftrivial-auto-var-init=uninitialized -I$(SRCROOT) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libcopyfile.dylib \
		-compatibility_version 1 -current_version 240 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -ldyld -lcompiler_rt -lsystem_kernel \
		-lsystem_malloc -lsystem_c -lsystem_blocks -lsystem_asl -lsystem_info -lxpc \
		-ldispatch -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/copyfile.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/copyfile.h $(SRCROOT)/xattr_flags.h,usr/include)
	$(call copy_headers,$(SRCROOT)/copyfile_private.h $(SRCROOT)/xattr_properties.h,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
