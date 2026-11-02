# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := syslog
TAG     := syslog-406
include ../common.mk

S       := $(SRCROOT)
INC     := $(S)/libsystem_asl.tproj/include
DERIVED := $(OBJ)/derived
SOURCES := $(addprefix libsystem_asl.tproj/src/,asl_mt_shim.c asl_client.c asl.c asl_core.c \
	asl_string.c asl_fd.c asl_msg_list.c asl_file.c asl_legacy1.c asl_msg.c asl_store.c \
	asl_util.c asl_object.c syslog.c)
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES)) $(OBJ)/asl_ipcUser.o
DYLIB   := $(OBJ)/libsystem_asl.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu11 -I$(INC) -I$(S)/aslcommon -I$(DERIVED) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include -Wno-deprecated-declarations
MIGFLAGS := -arch $(ARCH) -I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-I$(SDK)/usr/include -I$(SDK)/usr/local/include -I$(S)/aslcommon

LDLIBS := -L$(SDK)/usr/local/lib -lCrashReporterClient -L$(SDK)/usr/lib/system \
	-lcompiler_rt -ldyld -lsystem_kernel -lsystem_platform -lsystem_pthread \
	-lsystem_malloc -lsystem_c -lunwind -lsystem_blocks -ldispatch -lxpc -lsystem_trace \
	-lsystem_notify -lsystem_darwin -Wl,-upward-lsystem_info

all: $(DYLIB)

$(DERIVED)/.mig: $(S)/.fetched
	@mkdir -p $(DERIVED)
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header asl_ipc.h -user asl_ipcUser.c \
		-server /dev/null -sheader /dev/null $(S)/aslcommon/asl_ipc.defs
	touch $@
$(DERIVED)/asl_ipcUser.c: $(DERIVED)/.mig

$(OBJ)/%.o: $(S)/%.c $(DERIVED)/.mig
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(DERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib -all_load \
		-install_name /usr/lib/system/libsystem_asl.dylib \
		-compatibility_version 1 -current_version 406 -Wl,-umbrella,System \
		$(OBJS) $(LDLIBS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/syslog.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(INC)/asl.h,usr/include)
	$(call copy_headers,$(addprefix $(INC)/,asl_client.h asl_string.h asl_object.h \
		asl_msg_list.h asl_core.h asl_file.h asl_legacy1.h asl_msg.h asl_private.h \
		asl_store.h),usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
