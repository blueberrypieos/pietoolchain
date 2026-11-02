# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Libc
TAG     := Libc-1752.120.2
include ../common.mk

DERIVED  := $(OBJ)/derived
FEATURES := $(DERIVED)/$(ARCH)/libc-features.h
VARIANT_SYSHDRS := $(DERIVED)/System.framework/Versions/B/PrivateHeaders
SYSHDRS  := $(SDK)/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders
DYLIB    := $(OBJ)/libsystem_c.dylib

EXCLUDE := darwin/kvm.c gen/nlist.c sys/OSMemoryNotification.c
lst = $(filter-out $(EXCLUDE),$(shell cut -f1 lists/$(1).list))

ARCHIVES := Platform Base FreeBSD NetBSD TRE vCancelable vDarwinExtsn FortifySource
Platform_SRCS      := darwin/forceLibcToBuild.c
Base_SRCS          := $(call lst,Base)
FreeBSD_SRCS       := $(call lst,FreeBSD)
NetBSD_SRCS        := $(call lst,NetBSD)
TRE_SRCS           := $(call lst,TRE)
FortifySource_SRCS := $(call lst,FortifySource)

vsel = darwin/forceLibcToBuild.c \
	$(foreach f,$(2),$(filter %/$(f),$(shell cut -f1 lists/$(1).list)))
vCancelable_SRCS  := $(call vsel,Variant_Cancelable,creat.c sigcompat.c lockf.c nanosleep.c \
	pause.c sleep.c termios.c usleep.c wait.c waitpid.c recv.c send.c system.c)
vDarwinExtsn_SRCS := $(call vsel,Variant_DarwinExtsn,popen.c fdopen.c fopen.c realpath.c \
	getgroups.c)
c_dyld_SRCS := $(call vsel,libc_dyld,arc4random.c closedir.c dirfd.c getcwd.c getpagesize.c \
	nanosleep.c opendir.c readdir.c scandir.c sysctl.c sysctlbyname.c telldir.c usleep.c \
	atexit.c exit.c gettimeofday.c heapsort.c merge.c qsort.c reallocf.c realpath.c \
	bcopy.c strcat.c strdup.c strrchr.c _libc_init.c subsystem.c)

$(OBJ)/flags.mk: $(wildcard lists/*.list) Makefile
	mkdir -p $(OBJ)
	for a in Base FreeBSD NetBSD TRE FortifySource vCancelable vDarwinExtsn c_dyld; do \
		case $$a in vCancelable) l=Variant_Cancelable;; vDarwinExtsn) l=Variant_DarwinExtsn;; \
			c_dyld) l=libc_dyld;; *) l=$$a;; esac; \
		awk -F'\t' -v a=$$a 'NF > 1 { sub(/\.c$$/, ".o", $$1); \
			print "$$(OBJ)/" a "/" $$1 ": FILE_FLAGS = " $$2 }' lists/$$l.list; \
	done > $@
-include $(OBJ)/flags.mk

objs = $(patsubst %.c,$(OBJ)/$(1)/%.o,$($(1)_SRCS))

SRCROOT_SEARCH_PATHS := $(SRCROOT) $(SRCROOT)/include $(SRCROOT)/gen $(SRCROOT)/locale \
	$(SRCROOT)/locale/FreeBSD $(SRCROOT)/stdtime/FreeBSD $(SRCROOT)/darwin
FreeBSD_SEARCH_PATHS := $(SRCROOT)/fbsdcompat $(SRCROOT)/gdtoa $(SRCROOT)/gdtoa/FreeBSD
NetBSD_SEARCH_PATHS  := $(SRCROOT)/nbsdcompat
TRE_SEARCH_PATHS     := $(SRCROOT)/regex/TRE $(SRCROOT)/regex/FreeBSD
FreeBSD_CFLAGS := -include $(SRCROOT)/fbsdcompat/_fbsd_compat_.h
NetBSD_CFLAGS  := -include $(SRCROOT)/nbsdcompat/_nbsd_compat_.h
TRE_CFLAGS     := -DHAVE_CONFIG_H
vCancelable_SEARCH_PATHS  := $(FreeBSD_SEARCH_PATHS)
vDarwinExtsn_SEARCH_PATHS := $(FreeBSD_SEARCH_PATHS)
vCancelable_CFLAGS  := -DBUILDING_VARIANT -DVARIANT_CANCELABLE
vDarwinExtsn_CFLAGS := -DBUILDING_VARIANT -DVARIANT_DARWINEXTSN
vCancelable_SYSHDRS  := $(VARIANT_SYSHDRS)
vDarwinExtsn_SYSHDRS := $(VARIANT_SYSHDRS)
c_dyld_SEARCH_PATHS := $(FreeBSD_SEARCH_PATHS)
c_dyld_CFLAGS := -UBUILDING_VARIANT -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN \
	-U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 -fno-stack-check
c_dyld_SYSHDRS := $(VARIANT_SYSHDRS)
darwin_SRCS := $(shell cut -f1 lists/libdarwin.list)
darwin_LATE_PATHS := $(SYSHDRS) $(SRCROOT)/libdarwin $(SRCROOT)/libdarwin/h $(SDK)/usr/local/include
darwin_CFLAGS := -D_LIBC_NO_FEATURE_VERIFICATION=1 -DDARWIN_BUILDING_LIBSYSTEM_DARWIN=1 \
	-DOS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE -include pie/internal_decls.h

cflags = $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) -std=gnu11 \
	-fdollars-in-identifiers -fno-common \
	-D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
	-D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 $($(1)_CFLAGS) $(FILE_FLAGS) \
	$(addprefix -I,$($(1)_SEARCH_PATHS) $(DERIVED)/dtrace $(SRCROOT_SEARCH_PATHS) $($(1)_LATE_PATHS)) \
	-isystem $(or $($(1)_SYSHDRS),$(SYSHDRS)) \
	-I$(DERIVED)/$(ARCH) -I$(DERIVED) \
	-Wno-int-conversion -Wno-incompatible-function-pointer-types

collections_SRCS := collections/Source/collections_set.c collections/Source/collections_map.c
collections_HDRS := $(addprefix $(SRCROOT)/collections/PublicHeader/,collections.h \
	collections_map.h collections_set.h _collections_map.h _collections_set.h \
	_collections_map.in.h _collections_set.in.h)
collections_OBJS := $(patsubst %.c,$(OBJ)/collections/%.o,$(collections_SRCS))
COLLECTIONS_DYLIB := $(OBJ)/libsystem_collections.dylib

all: $(DYLIB) $(OBJ)/libc_dyld.a $(OBJ)/libsystem_darwin.dylib $(COLLECTIONS_DYLIB)

$(FEATURES): $(SRCROOT)/.fetched
	mkdir -p $(DERIVED)
	cd $(SRCROOT) && env ARCHS=$(ARCH) VARIANT_PLATFORM_NAME=macosx SRCROOT=$(SRCROOT) \
		DERIVED_FILES_DIR=$(DERIVED) perl xcodescripts/generate_features.pl > /dev/null

$(VARIANT_SYSHDRS)/.done: $(SRCROOT)/.fetched
	perl $(SRCROOT)/xcodescripts/patch_headers_variants.pl $(SYSHDRS) \
		$(DERIVED)/System.framework/Versions/B > /dev/null
	touch $@

define archive_rules
$(1)_OBJS := $$(call objs,$(1))
$$($(1)_OBJS): $(OBJ)/$(1)/%.o: $(SRCROOT)/%.c $(FEATURES) $(VARIANT_SYSHDRS)/.done $(OBJ)/flags.mk
	@mkdir -p $$(dir $$@)
	cd $(SRCROOT) && $(CC) $$(call cflags,$(1)) -c $$< -o $$@
$(OBJ)/lib$(1).a: $$($(1)_OBJS)
	rm -f $$@
	$(LIBTOOL) -static -no_warning_for_no_symbols -o $$@ $$^
endef
dylib_SRCS := darwin/forceLibcToBuild.c darwin/compatibility_hacks.c
$(foreach a,$(ARCHIVES) dylib c_dyld darwin,$(eval $(call archive_rules,$(a))))
$(OBJ)/libc_dyld.a: $(SOURCE_NOTICE_OBJ)

ARCHIVE_FILES := $(patsubst %,$(OBJ)/lib%.a,$(ARCHIVES))

$(OBJ)/interposable.list $(OBJ)/unexport.list: $(ARCHIVE_FILES)
	rm -f $(OBJ)/interposable.list $(OBJ)/unexport.list
	set -e; for a in $^; do \
		$(NM) -AUamgf $$a | grep '__TEXT,__text' | grep -vE '\$$VARIANT' \
			| awk '{ print $$NF }' >> $(OBJ)/interposable.list; \
		$(NM) -AUamgf $$a | awk '/\$$VARIANT/ { print $$NF }' >> $(OBJ)/unexport.list; \
	done

LDLIBS := -L$(SDK)/usr/local/lib -lCrashReporterClient \
	-L$(SDK)/usr/lib/system -lcompiler_rt -ldyld -lsystem_kernel -lsystem_m \
	-lsystem_malloc -lsystem_platform -lsystem_pthread \
	-Wl,-upward-ldispatch -Wl,-upward-lmacho -Wl,-upward-lsystem_asl \
	-Wl,-upward-lsystem_blocks -Wl,-upward-lsystem_info -Wl,-upward-lsystem_notify \
	-Wl,-upward-lxpc -Wl,-upward-lcorecrypto -Wl,-upward-lsystem_trace

$(DYLIB): $(dylib_OBJS) $(ARCHIVE_FILES) $(OBJ)/interposable.list $(OBJ)/unexport.list
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_c.dylib \
		-compatibility_version 1 -current_version 1752.120.2 -Wl,-umbrella,System \
		$(dylib_OBJS) -Wl,-all_load $(ARCHIVE_FILES) $(LDLIBS) \
		-Wl,-interposable_list,$(OBJ)/interposable.list \
		-Wl,-unexported_symbols_list,$(OBJ)/unexport.list \
		-Wl,-alias_list,$(SRCROOT)/xcodescripts/alias.list -o $@

DARWIN_DYLIB := $(OBJ)/libsystem_darwin.dylib
$(DARWIN_DYLIB): $(darwin_OBJS) $(DYLIB)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_darwin.dylib \
		-compatibility_version 1 -current_version 1752.120.2 -Wl,-umbrella,System \
		$(darwin_OBJS) -L$(SDK)/usr/lib/system -lcompiler_rt -ldyld -lsystem_kernel \
		-lsystem_m -lsystem_malloc -lsystem_blocks -lsystem_platform -lsystem_pthread \
		$(DYLIB) -ldispatch -lxpc -lmacho -Wl,-upward-lsystem_trace -o $@

$(collections_OBJS): $(OBJ)/collections/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -fvisibility=hidden -I$(SRCROOT) \
		-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders \
		-isystem $(SDK)/usr/local/include -c $< -o $@
$(COLLECTIONS_DYLIB): $(collections_OBJS) $(DYLIB)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libsystem_collections.dylib \
		-compatibility_version 1 -current_version 1752.120.2 -Wl,-umbrella,System \
		$(collections_OBJS) -L$(SDK)/usr/lib/system -ldyld -lcompiler_rt -lsystem_kernel \
		-lsystem_malloc $(DYLIB) -lsystem_blocks -o $@

install-libc: $(DYLIB) $(OBJ)/libc_dyld.a installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib/dyld
	install -m 644 $(OBJ)/libc_dyld.a $(SDK)/usr/local/lib/dyld/

install: install-libc $(DARWIN_DYLIB) $(COLLECTIONS_DYLIB)
	$(call install_both,$(DARWIN_DYLIB),usr/lib/system)
	$(call install_both,$(COLLECTIONS_DYLIB),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/Libc.xcodeproj)

installhdrs: $(SRCROOT)/.fetched
	rm -rf $(OBJ)/hdrs
	mkdir -p $(OBJ)/hdrs
	cd $(SRCROOT) && env SRCROOT=$(SRCROOT) DSTROOT=$(OBJ)/hdrs SDK_INSTALL_HEADERS_ROOT= \
		DEPLOYMENT_LOCATION=YES PLATFORM_NAME=macosx VARIANT_PLATFORM_NAME=macosx \
		SDKROOT=$(SDK) ARCHS=$(ARCH) CURRENT_ARCH=$(ARCH) \
		bash -e xcodescripts/headers.sh > $(OBJ)/headers.log 2>&1
	chmod -R u+w $(OBJ)/hdrs
	cp -a --remove-destination $(OBJ)/hdrs/. $(SDK)/
	$(call copy_headers,$(addprefix $(SRCROOT)/libdarwin/h/,ctl.h mach_exception.h bsd.h \
		err.h errno.h stdio.h cleanup.h stdlib.h mach_utils.h libdarwin_init.h string.h) \
		$(addprefix $(SRCROOT)/os/,linker_set.h api.h boot_mode_private.h variant_private.h),usr/local/include/os)
	$(call copy_headers,$(collections_HDRS),usr/local/include/os)

clean:
	rm -rf $(OBJ)

.PHONY: all install install-libc installhdrs clean
.SECONDARY:
