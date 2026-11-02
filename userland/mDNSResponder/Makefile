# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := mDNSResponder
TAG     := mDNSResponder-2881.120.11
include ../common.mk

S := $(SRCROOT)/mDNSShared
CLIENT_SRCS := dnssd_clientlib.c dnssd_clientstub.c dnssd_ipc.c dnssd_errstring.c
CLIENT_OBJS := $(addprefix $(OBJ)/,$(CLIENT_SRCS:.c=.o))
DYLIB := $(OBJ)/libsystem_dnssd.dylib
FLAGS := $(BASE_CFLAGS) -fwrapv -I$(S) -I$(SRCROOT)/mDNSCore -I$(S)/utilities \
	-isystem $(SDK)/usr/local/include -DMDNS_NO_STRICT=1 \
	'-DMDNS_UDS_SERVERPATH="/var/run/mDNSResponder"'

DAEMON := $(OBJ)/mdnsd/mdnsd

all: $(DYLIB) $(DAEMON)

$(DAEMON): $(SRCROOT)/.fetched
	$(MAKE) -C $(SRCROOT)/mDNSPosix os=x Daemon CC=$(CC) LD=$(CC) STRIP=true \
		OBJDIR=$(OBJ)/mdnsd/obj BUILDDIR=$(OBJ)/mdnsd \
		"CFLAGS=$(BASE_CFLAGS) -isystem $(SDK)/usr/local/include" \
		"CFLAGS_OS=-DHAVE_IPV6 -DHAVE_STRLCPY=1 -D__APPLE_USE_RFC_2292 -Wno-unused-but-set-variable \
		-UMDNS_UDS_SERVERPATH '-DMDNS_UDS_SERVERPATH=\"/var/run/mDNSResponder\"' \
		-UPID_FILE '-DPID_FILE=\"/var/run/mDNSResponder.pid\"'" \
		"LINKOPTS=$(BASE_LDFLAGS)"

installhdrs: fetch
	$(call copy_headers,$(S)/dns_sd.h,usr/include)
	$(call copy_headers,$(S)/dns_sd_private.h,usr/local/include)

$(OBJ)/%.o: $(S)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(CLIENT_OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_dnssd.dylib \
		-compatibility_version 1 -current_version 2881 -Wl,-umbrella,System \
		$^ -L$(SDK)/usr/lib/system -lsystem_c -lsystem_kernel -lsystem_platform \
		-lsystem_pthread -lsystem_malloc -lsystem_info -lsystem_blocks -ldispatch -lsystem_asl -ldyld -lcompiler_rt -o $@

install-lib: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

install: install-lib $(DAEMON)
	install -d $(ROOT)/usr/sbin
	install -m 755 $(DAEMON) $(ROOT)/usr/sbin/mDNSResponder
	install -d $(ROOT)/usr/share/man/man8
	install -m 644 $(SRCROOT)/mDNSShared/mDNSResponder.8 $(ROOT)/usr/share/man/man8/

clean:
	rm -rf $(OBJ)

.PHONY: all install install-lib installhdrs clean
