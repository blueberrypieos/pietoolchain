# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
# PIECRYPTO=DIR builds a checkout instead, as it is (uncommitted changes
# too).

PROJECT := libcorecrypto
TAG     := 0.2.0
EPOCH   := 1
COMMIT  := 6df3fe62c946fd0dbfa74fd105c854187cdcdab9
REPO_URL ?= https://github.com/blueberrypieos/piecrypto.git
include ../common.mk

PIECRYPTO ?= $(SRCROOT)
ifeq ($(PIECRYPTO),$(SRCROOT))
FETCHED := $(SRCROOT)/.verified
$(FETCHED): $(SRCROOT)/.fetched Makefile
	test ! -e $(VENDOR)/.git || test "$$(git -C $(VENDOR) rev-parse HEAD)" = "$(COMMIT)"
	touch $@
$(SRCROOT)/sources.mk: $(FETCHED)
	touch $@
endif
include $(PIECRYPTO)/sources.mk

OBJS   := $(patsubst $(PIECRYPTO)/%.c,$(OBJ)/%.o,$(PIECRYPTO_SRCS))
A64OBJ := $(patsubst $(PIECRYPTO)/%.c,$(OBJ)/%.o,$(PIECRYPTO_ARM64_SRCS))
STATIC := $(OBJ)/libcorecrypto_static.a
DYLIB  := $(OBJ)/libcorecrypto.dylib

CFLAGS := $(BASE_CFLAGS) -std=gnu11 -Wall $(PIECRYPTO_CPPFLAGS)

all: $(DYLIB) $(STATIC)

$(OBJ)/%.o: $(PIECRYPTO)/%.c $(FETCHED)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(A64OBJ): $(OBJ)/%.o: $(PIECRYPTO)/%.c $(FETCHED)
	mkdir -p $(dir $@)
	$(CC) $(filter-out -mcpu=%,$(CFLAGS)) $(PIECRYPTO_ARM64_CFLAGS) -c $< -o $@

$(STATIC): $(OBJS) $(A64OBJ)
	rm -f $@
	$(LIBTOOL) -static -o $@ $^

$(DYLIB): $(OBJS) $(A64OBJ)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libcorecrypto.dylib \
		-compatibility_version 1 -current_version 1 -Wl,-umbrella,System \
		-Wl,-exported_symbol,'_cc*' -Wl,-exported_symbol,'_CC*_FAULT_CANARY' \
		$^ -L$(SDK)/usr/lib/system -lsystem_kernel -lsystem_platform -lsystem_malloc \
		-lcompiler_rt -Wl,-upward-lsystem_c -o $@

install: $(DYLIB) $(STATIC) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib
	install -m 644 $(STATIC) $(SDK)/usr/local/lib/
	install -d $(ROOT)/DEBIAN
	install -m 755 preinst $(ROOT)/DEBIAN/preinst

installhdrs: $(FETCHED)
	rm -rf $(SDK)/usr/local/include/corecrypto
	$(call copy_headers,$(wildcard $(PIECRYPTO)/include/corecrypto/*.h),usr/local/include/corecrypto)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
