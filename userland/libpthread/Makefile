# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libpthread
TAG     := libpthread-539.100.4
include ../common.mk

UNIFDEF := -U__PTHREAD_BUILDING_PTHREAD__

SOURCES := src/resolver/resolver.c src/pthread.c src/pthread_cancelable.c \
	src/pthread_cond.c src/pthread_mutex.c src/qos.c src/pthread_rwlock.c \
	src/pthread_cwd.c src/pthread_tsd.c src/variants/pthread_cancelable_cancel.c \
	src/pthread_atfork.c src/pthread_dependency.c src/pthread_asm.s
OBJS  := $(patsubst %,$(OBJ)/obj/%.o,$(basename $(SOURCES)))
DYLIB := $(OBJ)/libsystem_pthread.dylib
PTHREAD_LDLIBS := -ldyld -Wl,-upward-lmacho

CFLAGS := $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) -std=gnu11 \
	-fno-stack-protector -fno-stack-check -fno-builtin -momit-leaf-frame-pointer \
	-D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 \
	-D__DARWIN_NON_CANCELABLE=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
	-D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
	-I$(SRCROOT)/src/resolver -I$(SRCROOT)/private -I$(SRCROOT)/include -I$(SRCROOT) \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
	-include pie/internal_decls.h \
	-Wno-int-conversion -Wno-sign-compare -Wno-unused-parameter

DYLD_SOURCES := src/resolver/resolver.c src/qos.c src/pthread.c src/pthread_cancelable.c \
	src/pthread_cond.c src/pthread_mutex.c src/pthread_dependency.c src/pthread_rwlock.c \
	src/pthread_tsd.c src/pthread_cwd.c src/pthread_atfork.c
DYLD_OBJS := $(patsubst %.c,$(OBJ)/dyld/%.o,$(DYLD_SOURCES))
DYLD_LIB  := $(OBJ)/libpthread_dyld.a

all: $(DYLIB) $(DYLD_LIB)

$(OBJ)/dyld/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DVARIANT_STATIC=1 -DVARIANT_DYLD=1 -c $< -o $@

$(DYLD_LIB): $(DYLD_OBJS) $(SOURCE_NOTICE_OBJ)
	rm -f $@
	$(LIBTOOL) -static -no_warning_for_no_symbols -o $@ $^

$(OBJ)/obj/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/obj/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	mkdir -p $(dir $@)
	$(CC) $(filter-out -include pie/internal_decls.h,$(CFLAGS)) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_pthread.dylib \
		-compatibility_version 1 -current_version 539.100.4 \
		-Wl,-umbrella,System -Wl,-alias_list,$(SRCROOT)/xcodescripts/pthread.aliases \
		-Wl,-dirty_data_list,$(SRCROOT)/xcodescripts/pthread.dirty \
		$(OBJS) -L$(SDK)/usr/local/lib -lCrashReporterClient \
		-L$(SDK)/usr/lib/system -lsystem_kernel -lsystem_platform \
		$(PTHREAD_LDLIBS) -o $@

install: $(DYLIB) $(DYLD_LIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib/dyld
	install -m 644 $(DYLD_LIB) $(SDK)/usr/local/lib/dyld/
	$(call install_xcode_man,$(SRCROOT)/libpthread.xcodeproj)

clean:
	rm -rf $(OBJ)

installhdrs: $(SRCROOT)/.fetched
	$(call copy_headers,$(wildcard $(SRCROOT)/include/pthread/*.h),usr/include/pthread,$(UNIFDEF))
	$(call copy_headers,$(wildcard $(SRCROOT)/private/pthread/*.h),usr/local/include/pthread,$(UNIFDEF))
	$(call copy_headers,$(wildcard $(SRCROOT)/include/sys/*.h),usr/include/sys,$(UNIFDEF))
	$(call copy_headers,$(wildcard $(SRCROOT)/include/sys/_pthread/*.h),usr/include/sys/_pthread,$(UNIFDEF))
	$(call copy_headers,$(wildcard $(SRCROOT)/private/sys/*.h),usr/local/include/sys,$(UNIFDEF))
	ln -sf pthread/pthread.h $(SDK)/usr/include/pthread.h
	ln -sf pthread/pthread_impl.h $(SDK)/usr/include/pthread_impl.h
	ln -sf pthread/pthread_spis.h $(SDK)/usr/include/pthread_spis.h
	ln -sf pthread/sched.h $(SDK)/usr/include/sched.h
	ln -sf pthread/posix_sched.h $(SDK)/usr/local/include/posix_sched.h
	ln -sf pthread/spinlock_private.h $(SDK)/usr/local/include/pthread_spinlock.h
	ln -sf pthread/workqueue_private.h $(SDK)/usr/local/include/pthread_workqueue.h
	install -d $(SDK)/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders
	ln -sf ../../../../../../../usr/local/include/pthread/tsd_private.h \
		$(SDK)/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders/pthread_machdep.h

.PHONY: all install installhdrs clean
