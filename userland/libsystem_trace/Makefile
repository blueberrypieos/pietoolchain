# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_trace
include ../common.mk

DYLIB := $(OBJ)/libsystem_trace.dylib
OBJS  := $(OBJ)/trace.o $(OBJ)/activity.o

all: $(DYLIB)

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -fblocks -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_trace.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -lsystem_c -lsystem_malloc -lsystem_kernel -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib/system)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
