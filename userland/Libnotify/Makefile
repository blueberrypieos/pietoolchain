# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Libnotify
TAG     := Libnotify-348.120.4
include ../common.mk

S       := $(SRCROOT)
DERIVED := $(OBJ)/derived
SOURCES := libnotify.c notify_client.c table.c
MIG_C   := $(DERIVED)/notify_ipcUser.c $(DERIVED)/notify_old_ipcUser.c
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(patsubst $(DERIVED)/%.c,$(OBJ)/%.o,$(MIG_C))
DYLIB   := $(OBJ)/libsystem_notify.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu11 -fvisibility=hidden -fno-exceptions \
	-D__DARWIN_NON_CANCELABLE=1 -I$(S) -I$(DERIVED) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include
MIGFLAGS := -arch $(ARCH) -I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-I$(SDK)/usr/include -I$(SDK)/usr/local/include -I$(S)

LDLIBS := -L$(SDK)/usr/local/lib -lCrashReporterClient -L$(SDK)/usr/lib/system -ldyld -lcompiler_rt -lsystem_kernel \
	-lsystem_platform -lsystem_pthread -lsystem_malloc -lsystem_c -lsystem_blocks \
	-ldispatch -lxpc -lsystem_darwin -lsystem_trace -lsystem_collections

DDERIVED := $(OBJ)/notifyd-derived
NOTIFYD_SRCS := notifyd/notifyd.c notifyd/notify_proc.c notifyd/pathwatch.c notifyd/service.c \
	table.c libnotify.c
NOTIFYD_OBJS := $(patsubst %.c,$(OBJ)/notifyd/%.o,$(NOTIFYD_SRCS)) \
	$(OBJ)/notifyd/notify_ipcServer.o $(OBJ)/notifyd/notifyServer.o
NOTIFYD := $(OBJ)/bin/notifyd
NOTIFYUTIL := $(OBJ)/notifyutil
DFLAGS := $(FLAGS) -fvisibility=default -DSINGLE_THREADED_NOTIFY_STATE=1 -I$(S)/notifyd -I$(DDERIVED)

all: $(DYLIB) $(NOTIFYD) $(NOTIFYUTIL)

$(DDERIVED)/.mig: $(S)/.fetched
	@mkdir -p $(DDERIVED)
	cd $(DDERIVED) && $(MIG) $(MIGFLAGS) -header notify_ipc.h -user /dev/null \
		-server notify_ipcServer.c -sheader notify_ipcServer.h $(S)/notify_ipc.defs
	cd $(DDERIVED) && $(MIG) $(MIGFLAGS) -DMACH_NOTIFY_SEND_POSSIBLE_EXPECTED=1 -header /dev/null \
		-user /dev/null -server notifyServer.c -sheader notifyServer.h $(SDK)/usr/include/mach/notify.defs
	touch $@
$(DDERIVED)/notify_ipcServer.c $(DDERIVED)/notifyServer.c: $(DDERIVED)/.mig

$(OBJ)/notifyd/%.o: $(S)/%.c $(DDERIVED)/.mig $(DERIVED)/notify_probes.h
	@mkdir -p $(dir $@)
	$(CC) $(DFLAGS) -c $< -o $@
$(OBJ)/notifyd/%.o: $(DDERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DFLAGS) -c $< -o $@

$(NOTIFYD): $(NOTIFYD_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(BASE_LDFLAGS) $^ -L$(SDK)/usr/lib -lbsm -L$(SDK)/usr/local/lib -lCrashReporterClient -o $@

$(OBJ)/notifyutil.o: $(S)/notifyutil/notifyutil.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -fvisibility=default -c $< -o $@
$(NOTIFYUTIL): $(OBJ)/notifyutil.o
	$(CC) $(BASE_LDFLAGS) $< -o $@

$(DERIVED)/notify_probes.h: $(S)/notify_probes.d
	@mkdir -p $(DERIVED)
	$(PIE)/userland/tools/dtrace-h.py $< $@

$(DERIVED)/.mig: $(S)/.fetched
	@mkdir -p $(DERIVED)
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header notify_ipc.h -user notify_ipcUser.c \
		-server /dev/null -sheader /dev/null $(S)/notify_ipc.defs
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header notify_old_ipc.h -user notify_old_ipcUser.c \
		-server /dev/null -sheader /dev/null $(S)/notify_old_ipc.defs
	touch $@
$(MIG_C): $(DERIVED)/.mig

$(OBJ)/%.o: $(S)/%.c $(DERIVED)/.mig $(DERIVED)/notify_probes.h
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(DERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_notify.dylib \
		-compatibility_version 1 -current_version 348.120.4 -Wl,-umbrella,System \
		$(OBJS) $(LDLIBS) -o $@

install-lib: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)

install: install-lib $(NOTIFYD) $(NOTIFYUTIL)
	install -d $(ROOT)/usr/sbin $(ROOT)/usr/bin $(ROOT)/etc
	install -m 755 $(NOTIFYD) $(ROOT)/usr/sbin/notifyd
	install -m 755 $(NOTIFYUTIL) $(ROOT)/usr/bin/notifyutil
	install -m 644 $(S)/notifyd/notify.conf.MacOSX $(ROOT)/etc/notify.conf
	$(call install_xcode_man,$(SRCROOT)/Libnotify.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(S)/notify.h $(S)/notify_keys.h,usr/include)
	$(call copy_headers,$(S)/notify_private.h,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install install-lib installhdrs clean
