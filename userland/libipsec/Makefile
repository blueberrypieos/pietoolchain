# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libipsec
REPO    := ipsec
TAG     := ipsec-1125
include ../common.mk

S       := $(SRCROOT)/ipsec-tools/libipsec
DERIVED := $(OBJ)/derived
OBJS    := $(OBJ)/ipsec_dump_policy.o $(OBJ)/ipsec_get_policylen.o $(OBJ)/ipsec_strerror.o \
	$(OBJ)/policy_parse.o $(OBJ)/policy_token.o
DYLIB   := $(OBJ)/libipsec.A.dylib
FLAGS   := $(BASE_CFLAGS) -DHAVE_CONFIG_H=1 -I$(S) -I$(SRCROOT)/ipsec-tools/Common -I$(DERIVED) \
	-I$(SRCROOT)/ipsec-tools/racoon \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders

all: $(DYLIB)

$(DERIVED)/policy_parse.c: $(S)/policy_parse.y $(SRCROOT)/.fetched
	@mkdir -p $(DERIVED)
	bison -y -d -p __libipsec -o $@ $<
	cp $(DERIVED)/policy_parse.h $(DERIVED)/y.tab.h
$(DERIVED)/policy_token.c: $(S)/policy_token.l $(DERIVED)/policy_parse.c
	flex -P__libipsec -o $@ $<

$(OBJ)/%.o: $(S)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(DERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libipsec.A.dylib \
		-compatibility_version 1 -current_version 300 $(OBJS) -o $@

install: $(DYLIB)
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libipsec.A.dylib $(SDK)/usr/lib/libipsec.dylib
	ln -sf libipsec.A.dylib $(ROOT)/usr/lib/libipsec.dylib
	$(call install_xcode_man,$(SRCROOT)/ipsec.xcodeproj)

installhdrs:

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
