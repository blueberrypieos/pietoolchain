# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT  := libxo
TAG      := 1.7.5
REPO_URL := https://github.com/Juniper/libxo
include ../common.mk

S       := $(SRCROOT)/libxo
SOURCES := libxo.c xo_encoder.c xo_syslog.c xo_tolower.c xo_toupper.c xo_utf8.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libxo.dylib
FLAGS   := $(BASE_CFLAGS) -I$(CURDIR) -I$(S) -DXO_ENCODERDIR=\"/usr/lib/libxo/encoder\" \
	-DXO_SHAREDIR=\"/usr/share/libxo\"

all: $(DYLIB)

$(OBJ)/%.o: $(S)/%.c $(SRCROOT)/.fetched xo_config.h
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libxo.dylib \
		-compatibility_version 1 -current_version 1.7.5 $(OBJS) -o $@

XO_MANS := $(filter-out %.1,$(notdir $(wildcard $(S)/*.[357])))

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	set -e; for m in $(XO_MANS); do install -d $(ROOT)/usr/share/man/man$${m##*.}; \
		install -m 644 $(S)/$$m $(ROOT)/usr/share/man/man$${m##*.}/; done

installhdrs: fetch
	$(call copy_headers,$(S)/xo.h $(S)/xo_encoder.h,usr/include/libxo)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
