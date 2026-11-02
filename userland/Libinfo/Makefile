# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Libinfo
TAG     := Libinfo-600
include ../common.mk

S := $(SRCROOT)

SOURCES := $(shell cat sources.list)
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libsystem_info.dylib

INCLUDES := $(addprefix -I$(S)/,Libinfo lookup.subproj membership.subproj \
	gen.subproj nis.subproj rpc.subproj dns.subproj util.subproj)
FLAGS := $(BASE_CFLAGS) -std=gnu99 -fvisibility=hidden \
	-DDEBUG=0 -D__DARWIN_NON_CANCELABLE=1 -D__MigTypeCheck=1 -DINET6=1 \
	-DCONFIG_MAC -DSYNTH_ROOTFS -DMDNS_AVAILABLE \
	-I$(SDK)/usr/local/include -I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	$(INCLUDES) -Wno-deprecated-declarations -Wno-deprecated-non-prototype

LDLIBS := -L$(SDK)/usr/lib/system -lcompiler_rt -ldyld -lsystem_kernel \
	-lsystem_malloc -lsystem_platform -lsystem_pthread -lsystem_c -ldispatch \
	-lsystem_asl -lsystem_blocks -lsystem_notify -lxpc -lsystem_trace \
	-lsystem_darwin -lsystem_featureflags -lsystem_dnssd

all: $(DYLIB)

$(OBJ)/%.o: $(S)/%.c $(S)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib -all_load \
		-install_name /usr/lib/system/libsystem_info.dylib \
		-compatibility_version 1 -current_version 600 -Wl,-umbrella,System \
		$(OBJS) $(LDLIBS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/Libinfo.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(S)/gen.subproj/ifaddrs.h $(S)/lookup.subproj/grp.h \
		$(S)/lookup.subproj/netdb.h $(S)/lookup.subproj/pwd.h \
		$(S)/membership.subproj/membership.h $(S)/membership.subproj/ntsid.h \
		$(S)/lookup.subproj/aliasdb.h $(S)/lookup.subproj/bootparams.h \
		$(S)/lookup.subproj/printerdb.h,usr/include)
	$(call copy_headers,$(S)/gen.subproj/configuration_profile.h $(S)/lookup.subproj/ils.h \
		$(S)/lookup.subproj/kvbuf.h $(S)/lookup.subproj/libinfo.h \
		$(S)/lookup.subproj/si_compare.h $(S)/lookup.subproj/si_data.h \
		$(S)/lookup.subproj/si_module.h $(S)/lookup.subproj/thread_data.h \
		$(S)/lookup.subproj/lookupPriv.h $(S)/lookup.subproj/libinfo_muser.h \
		$(S)/lookup.subproj/netdb_async.h $(S)/membership.subproj/membershipPriv.h,usr/local/include)
	$(call copy_headers,$(addprefix $(S)/rpc.subproj/,auth.h auth_unix.h clnt.h rpc.h \
		rpc_msg.h svc.h svc_auth.h types.h xdr.h pmap_clnt.h pmap_prot.h pmap_rmt.h),usr/include/rpc)
	$(call copy_headers,$(S)/nis.subproj/yp_prot.h $(S)/nis.subproj/ypclnt.h,usr/include/rpcsvc)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
