# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Librpcsvc
TAG     := Librpcsvc-31
include ../common.mk

RPCGEN ?= rpcgen
XFILES  = $(wildcard $(SRCROOT)/*.x)

all: installhdrs

installhdrs: fetch
	mkdir -p $(OBJ)
	set -e; for x in $(XFILES); do \
		h=$$(basename $$x .x).h; \
		rm -f $(OBJ)/$$h; \
		$(RPCGEN) -h -o $(OBJ)/$$h $$x; \
	done
	$(call copy_headers,$(XFILES) $(patsubst $(SRCROOT)/%.x,$(OBJ)/%.h,$(XFILES)),usr/include/rpcsvc)

install: installhdrs

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
