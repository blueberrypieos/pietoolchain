# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := OpenBSM
TAG     := OpenBSM-21
include ../common.mk

BSM     := $(SRCROOT)/openbsm/bsm
SOURCES := $(addprefix openbsm/libbsm/,bsm_audit.c bsm_class.c bsm_control.c bsm_domain.c \
	bsm_event.c bsm_flags.c bsm_io.c bsm_mask.c bsm_notify.c bsm_socket_type.c bsm_token.c \
	bsm_user.c bsm_wrappers.c bsm_errno.c bsm_fcntl.c)
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libbsm.0.dylib
FLAGS   := $(BASE_CFLAGS) -I$(SRCROOT)/openbsm -isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libbsm.0.dylib \
		-compatibility_version 1 -current_version 1 $(OBJS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libbsm.0.dylib $(SDK)/usr/lib/libbsm.dylib
	ln -sf libbsm.0.dylib $(ROOT)/usr/lib/libbsm.dylib
	$(call install_xcode_man,$(SRCROOT)/OpenBSM.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(filter-out $(BSM)/audit.h $(BSM)/audit_record.h $(BSM)/audit_kevents.h \
		$(BSM)/audit_internal.h,$(wildcard $(BSM)/*.h)),usr/include/bsm)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
