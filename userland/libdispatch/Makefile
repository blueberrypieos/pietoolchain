# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libdispatch
TAG     := libdispatch-1542.100.32
include ../common.mk

UNIFDEF_FLAGS := -U__DISPATCH_BUILDING_DISPATCH__ -U__linux__ -DTARGET_OS_WIN32=0 -U__ANDROID__

PUBLIC_HEADERS  = $(addprefix $(SRCROOT)/,$(shell awk '$$2=="Public"{print $$1}' headers.list))
PRIVATE_HEADERS = $(addprefix $(SRCROOT)/,$(shell awk '$$2=="Private"{print $$1}' headers.list))
OS_PUBLIC_HEADERS = $(addprefix $(SRCROOT)/os/,object.h workgroup.h workgroup_base.h \
	workgroup_interval.h workgroup_object.h workgroup_parallel.h clock.h)
OS_PRIVATE_HEADERS = $(addprefix $(SRCROOT)/os/,object_private.h voucher_private.h \
	voucher_activity_private.h workgroup_private.h workgroup_interval_private.h \
	workgroup_object_private.h eventlink_private.h)

SOURCES := resolver/resolver.c src/init.c src/object.c src/block.cpp src/shims/lock.c \
	src/semaphore.c src/once.c src/eventlink.c src/queue.c src/apply.c src/source.c \
	src/workgroup.c src/shims/yield.c src/mach.c src/event/event.c src/event/event_kevent.c \
	src/event/event_epoll.c src/voucher.c src/firehose/firehose_buffer.c src/io.c src/data.c \
	src/transform.c src/time.c src/allocator.c src/benchmark.c
DERIVED := $(OBJ)/derived
MIG_C := $(DERIVED)/protocolUser.c $(DERIVED)/protocolServer.c $(DERIVED)/firehoseUser.c \
	$(DERIVED)/firehose_replyServer.c
MIG_H := $(DERIVED)/protocol.h $(DERIVED)/protocolServer.h $(DERIVED)/firehose.h \
	$(DERIVED)/firehoseServer.h $(DERIVED)/firehose_reply.h $(DERIVED)/firehose_replyServer.h
OBJS := $(patsubst %,$(OBJ)/obj/%.o,$(basename $(SOURCES))) \
	$(patsubst $(DERIVED)/%.c,$(OBJ)/obj/mig/%.o,$(MIG_C))
DYLIB := $(OBJ)/libdispatch.dylib

FLAGS := $(BASE_CFLAGS) -fvisibility=hidden -momit-leaf-frame-pointer \
	-D__DARWIN_NON_CANCELABLE=1 -DUSE_OBJC=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
	-D__PTHREAD_EXPOSE_INTERNALS__=1 \
	-I$(SRCROOT) -I$(SRCROOT)/private -I$(SRCROOT)/src -I$(DERIVED) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-isystem $(SDK)/usr/local/include
MIGFLAGS := -novouchers -arch $(ARCH) \
	-I$(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-I$(SDK)/usr/include -I$(SDK)/usr/local/include -I$(SRCROOT) -I$(SRCROOT)/private -I$(SRCROOT)/src

all: installhdrs $(DYLIB)

$(DERIVED)/provider.h: $(SRCROOT)/src/provider.d
	@mkdir -p $(DERIVED)
	$(PIE)/userland/tools/dtrace-h.py $< $@

$(DERIVED)/.mig: $(SRCROOT)/.fetched
	@mkdir -p $(DERIVED)
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header protocol.h -sheader protocolServer.h \
		-user protocolUser.c -server protocolServer.c $(SRCROOT)/src/protocol.defs
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header firehose.h -sheader firehoseServer.h \
		-user firehoseUser.c -server /dev/null $(SRCROOT)/src/firehose/firehose.defs
	cd $(DERIVED) && $(MIG) $(MIGFLAGS) -header firehose_reply.h -sheader firehose_replyServer.h \
		-user /dev/null -server firehose_replyServer.c $(SRCROOT)/src/firehose/firehose_reply.defs
	touch $@
$(MIG_C) $(MIG_H): $(DERIVED)/.mig

$(OBJ)/obj/%.o: $(SRCROOT)/%.c $(DERIVED)/.mig $(DERIVED)/provider.h
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -std=gnu11 -c $< -o $@
$(OBJ)/obj/%.o: $(SRCROOT)/%.cpp $(DERIVED)/.mig $(DERIVED)/provider.h
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -std=gnu++17 -fno-exceptions -c $< -o $@
$(OBJ)/obj/mig/%.o: $(DERIVED)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -std=gnu11 -c $< -o $@

$(DYLIB): $(OBJS)
	$(CXX) $(BASE_LDFLAGS) -dynamiclib -nostdlib -install_name /usr/lib/system/libdispatch.dylib \
		-compatibility_version 1 -current_version 1542.100.32 -Wl,-umbrella,System \
		-Wl,-alias_list,$(SRCROOT)/xcodeconfig/libdispatch.aliases \
		-Wl,-order_file,$(SRCROOT)/xcodeconfig/libdispatch.order \
		$(OBJS) -L$(SDK)/usr/local/lib -lCrashReporterClient -L$(SDK)/usr/lib/system \
		-ldyld -lcompiler_rt -lsystem_kernel -lsystem_platform -lsystem_pthread \
		-lsystem_malloc -lsystem_c -lsystem_blocks -lunwind -Wl,-upward-lsystem_darwin -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/libdispatch.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(PUBLIC_HEADERS),usr/include/dispatch,$(UNIFDEF_FLAGS))
	$(call copy_headers,$(PRIVATE_HEADERS),usr/local/include/dispatch,$(UNIFDEF_FLAGS))
	$(call copy_headers,$(OS_PUBLIC_HEADERS),usr/include/os,$(UNIFDEF_FLAGS))
	$(call copy_headers,$(OS_PRIVATE_HEADERS),usr/local/include/os,$(UNIFDEF_FLAGS))


clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
