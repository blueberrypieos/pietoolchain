# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsystem_coreservices
include ../common.mk

DYLIB := $(OBJ)/libsystem_coreservices.dylib

all: $(DYLIB)

$(OBJ)/coreservices.o: coreservices.c dirhelper_priv.h
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -I. -c $< -o $@

$(OBJ)/sysdir.o: sysdir.c sysdir.h NSSystemDirectories.h
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -I. -c $< -o $@

$(DYLIB): $(OBJ)/coreservices.o $(OBJ)/sysdir.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_coreservices.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		$^ -L$(SDK)/usr/lib/system -lsystem_kernel -lsystem_c -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

installhdrs:
	install -d $(SDK)/usr/local/include
	install -m 644 dirhelper_priv.h $(SDK)/usr/local/include/dirhelper_priv.h
	install -m 644 sysdir.h NSSystemDirectories.h $(SDK)/usr/include/

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
