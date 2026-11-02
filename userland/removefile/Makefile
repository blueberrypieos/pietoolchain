# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := removefile
TAG     := removefile-85.100.6
include ../common.mk

SOURCES := removefile_random.c removefile_rename_unlink.c removefile_sunlink.c \
	removefile_tree_walker.c removefile.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libremovefile.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu11 -Wall -D__DARWIN_NON_CANCELABLE=1 -I$(SRCROOT) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libremovefile.dylib \
		-compatibility_version 1 -current_version 85.100.6 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -ldyld -lcompiler_rt -lsystem_kernel \
		-lsystem_platform -lsystem_malloc -lsystem_c -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/removefile.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/checkint.h $(SRCROOT)/removefile.h,usr/include)
	$(call copy_headers,$(SRCROOT)/removefile_priv.h,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
