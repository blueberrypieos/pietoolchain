# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libsbuf
include ../common.mk

DYLIB := $(OBJ)/libsbuf.dylib

all: $(DYLIB)

$(OBJ)/sbuf.o: sbuf.c usbuf.h
	mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -Wextra -c $< -o $@

$(DYLIB): $(OBJ)/sbuf.o
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libsbuf.dylib \
		-compatibility_version 1 -current_version 1 $< -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)

installhdrs:
	install -d $(SDK)/usr/include
	install -m 644 usbuf.h $(SDK)/usr/include/usbuf.h

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
