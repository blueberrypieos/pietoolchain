# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libpcap
TAG     := libpcap-146
include ../common.mk

S       := $(SRCROOT)/libpcap
DERIVED := $(OBJ)/derived
SOURCES := bpf_dump.c bpf_filter.c bpf_image.c etherent.c fad-getad.c fmtutils.c gencode.c \
	nametoaddr.c optimize.c pcap-bpf.c pcap-apple-stubs.c pcap-common.c pcap-darwin.c pcap-util.c \
	pcap.c pcapng.c savefile.c sf-pcap.c sf-pcapng.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(OBJ)/grammar.o $(OBJ)/scanner.o
DYLIB   := $(OBJ)/libpcap.A.dylib
FLAGS   := $(BASE_CFLAGS) -DHAVE_CONFIG_H -Dpcap_EXPORTS -I$(S) -I$(DERIVED) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(DERIVED)/grammar.c: $(S)/grammar.y $(SRCROOT)/.fetched
	@mkdir -p $(DERIVED)
	bison -p pcap_ -d -o $@ $<
$(DERIVED)/grammar.h: $(DERIVED)/grammar.c
$(DERIVED)/scanner.c: $(S)/scanner.l $(DERIVED)/grammar.h
	flex -P pcap_ --header-file=$(DERIVED)/scanner.h --nounput -o $@ $<
$(DERIVED)/scanner.h: $(DERIVED)/scanner.c

$(OBJ)/%.o: $(S)/%.c $(DERIVED)/scanner.h $(DERIVED)/grammar.h
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(DERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -Wno-unused-but-set-variable -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libpcap.A.dylib \
		-compatibility_version 1 -current_version 1 $(OBJS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libpcap.A.dylib $(SDK)/usr/lib/libpcap.dylib
	ln -sf libpcap.A.dylib $(ROOT)/usr/lib/libpcap.dylib
	$(call install_xcode_man,$(SRCROOT)/libpcap.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(S)/pcap.h $(S)/pcap-bpf.h $(S)/pcap-namedb.h,usr/include)
	$(call copy_headers,$(wildcard $(S)/pcap/*.h),usr/include/pcap)
	$(call copy_headers,$(S)/pcap-util.h,System/Library/Frameworks/System.framework/PrivateHeaders/pcap)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
