# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsysmon
include ../common.mk

DYLIB := $(OBJ)/libsysmon.dylib

all: $(DYLIB)

$(OBJ)/sysmon.o: sysmon.c sysmon.h
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -fblocks -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJ)/sysmon.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libsysmon.dylib \
		-compatibility_version 1 -current_version 1 $< -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)

installhdrs:
	install -d $(SDK)/usr/local/include
	install -m 644 sysmon.h $(SDK)/usr/local/include/sysmon.h

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
