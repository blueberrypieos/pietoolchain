# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# Shared debian/rules logic for a darwin-arm64 port that is packaged in its
# own repository and cross-compiled with pietoolchain. pietoolchain installs
# this file as /usr/share/pietoolchain/rules.mk.
#
# The repository's tree is the port's source (SRCROOT). The port's
# debian/rules includes this file and then defines the port's build recipe,
# which must have an install target that installs into $(ROOT):
#
#   #!/usr/bin/make -f
#   include /usr/share/pietoolchain/rules.mk
#   ...
#   install: ...
#
# The package is built from its extracted source package:
#
#   dpkg-source -x PORT_VERSION.dsc && cd PORT-VERSION
#   dpkg-buildpackage -a darwin-arm64 -b -us -uc
#
# Before the build, pie-source-guard checks that everything in the tree is
# present in the debian/copyright file. PIE_FULL_SOURCE=1 skips the check,
# for pie-trace's analysis build from the full upstream tree.
#
# The recipe can use CC, CXX, LIBTOOL, STRIP, NM, BASE_CFLAGS and
# BASE_LDFLAGS; SDK, the read-only SDK from pietoolchain-sdk; SYSROOT, what
# the port builds against (below); OBJ and ROOT, the object and install
# directories; PIETOOLCHAIN, the directory that holds this file, tools.mk
# and the project tools; and PIE_UPSTREAM_VERSION, debian/changelog's
# upstream version without the +dsN suffix of a source package that leaves
# upstream files out.
#
# Other ports' packages: a port that builds against another port's -dev
# package names it in Build-Depends, as libfoo-dev (>= 1.0-1).
# pie-cross-deps, installed beside this file, installs the build
# dependencies. The build machine's are installed as usual. The darwin-arm64
# ones come from the ports' repository and are unpacked into PIE_CROSS_ROOT,
# by default /var/lib/pietoolchain/cross-root. The build is then
# dpkg-buildpackage -a darwin-arm64 -d.
#
# SYSROOT is debian/build/sysroot: the SDK, linked, with the headers,
# libraries, pkg-config files and frameworks of those packages copied over
# it. With none, SYSROOT is the SDK itself. PIE_PKG_CONFIG is the target's
# pkg-config. It reads only SYSROOT's .pc files and prefixes their paths
# with SYSROOT. The build machine's own pkg-config is left as it is, for the
# build's native tools.
#
# MESON_CROSS and CMAKE_TOOLCHAIN are a meson cross file and a CMake
# toolchain file for the target, with the compilers, BASE_CFLAGS, SYSROOT
# and PIE_PKG_CONFIG. PATH has install_name_tool and otool by those names,
# for meson's install step.
#
# The build target runs the recipe's install target into debian/build/root.
# The binary target packages that root as the packages debian/control
# names. The first package gets the files the others do not take: each
# other package P takes those that debian/P.files names, one shell glob
# per line, relative to the root (usr/include/*, say); a glob that matches
# nothing stops the build. Every package gets Darwin's file system layout:
# /etc, /tmp and /var move under /private, the files in /etc become
# conffiles, debian/copyright is installed in /usr/share/doc/P, and every
# file is owned by root.
#
# For reproducible builds, the compiler maps the tree's paths to
# /usr/src/pie (PIE_REPRODUCIBLE); ld64 records its debug map relative to
# the tree (-oso_prefix) and without timestamps (ZERO_AR_DATE, which also
# removes the timestamps from static archives); the build runs in UTC with
# the C locale; and every packaged file is dated SOURCE_DATE_EPOCH, the date
# of the latest debian/changelog entry.

# The directory that holds this file. It must be set before any other
# makefile is included, since it is read from MAKEFILE_LIST.
PIETOOLCHAIN  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
include /usr/share/dpkg/pkg-info.mk

PIE_TOOLCHAIN ?= /usr/lib/pietoolchain/toolchain
TOOLCHAIN     := $(PIE_TOOLCHAIN)
SDK           := $(TOOLCHAIN)/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk
W             := $(CURDIR)/debian/build
SRCROOT       := $(CURDIR)
PIE_UPSTREAM_VERSION := $(firstword $(subst +ds, ,$(DEB_VERSION_UPSTREAM)))
OBJ           := $(W)/obj
ROOT          := $(W)/root

PIE_PACKAGES := $(shell sed -n 's/^Package: *//p' debian/control)
ifeq ($(PIE_PACKAGES),)
$(error rules.mk: debian/control names no package)
endif
PIE_PACKAGE := $(firstword $(PIE_PACKAGES))
PKG  := $(CURDIR)/debian/$(PIE_PACKAGE)
JOBS := $(or $(patsubst parallel=%,%,$(filter parallel=%,$(DEB_BUILD_OPTIONS))),1)

CC      := $(TOOLCHAIN)/bin/clang
CXX     := $(TOOLCHAIN)/bin/clang++
LIBTOOL := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-libtool
STRIP   := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-strip
NM      := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-nm
PIE     := $(W)/pie
export PATH := $(TOOLCHAIN)/bin:$(TOOLCHAIN)/prefix/bin:$(PIE)/bin:$(PATH)

# The darwin-arm64 packages the build depends on, unpacked by pie-cross-deps.
PIE_CROSS_ROOT ?= /var/lib/pietoolchain/cross-root
PIE_CROSS := $(if $(wildcard $(PIE_CROSS_ROOT)/*),yes)
ifeq ($(PIE_CROSS),)
SYSROOT := $(SDK)
else
SYSROOT := $(W)/sysroot
endif
# What a package gives the sysroot.
PIE_SYSROOT_DIRS := usr/include usr/local/include usr/lib usr/local/lib usr/share/pkgconfig \
	System/Library/Frameworks
PIE_PKG_CONFIG  := $(PIE)/bin/pkg-config-darwin
MESON_CROSS     := $(PIE)/meson-cross.ini
CMAKE_TOOLCHAIN := $(PIE)/toolchain.cmake

export PIE_REPRODUCIBLE := 1
export PIE_REPRO_SOURCE := $(SRCROOT)
export ZERO_AR_DATE     := 1
export TZ               := UTC
export LC_ALL           := C

# Set -mcpu to generic, plain ARMv8.0.
ARCH      := arm64
ARCHFLAGS := -arch $(ARCH) -mcpu=generic
MINVER    := -mmacosx-version-min=26.5
TARGET_OS_DEFS := -DTARGET_OS_EXCLAVECORE=0 -DTARGET_OS_EXCLAVEKIT=0
BASE_CFLAGS  := $(ARCHFLAGS) -isysroot $(SYSROOT) $(MINVER) $(TARGET_OS_DEFS) -Os -g \
	-fno-common -fstack-protector-strong
BASE_LDFLAGS := $(ARCHFLAGS) -isysroot $(SYSROOT) $(MINVER) -Wl,-adhoc_codesign \
	-Wl,-oso_prefix,$(CURDIR)/

pie_comma := ,
pie_space := $(subst ,, )
pie_meson_list = $(subst $(pie_space),$(pie_comma) ,$(patsubst %,'%',$(strip $(1))))

define PIE_MESON_CROSS_INI
[binaries]
c = '$(CC)'
cpp = '$(CXX)'
objc = '$(CC)'
ar = '$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-ar'
ranlib = '$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-ranlib'
strip = '$(STRIP)'
nm = '$(NM)'
pkg-config = '$(PIE_PKG_CONFIG)'
[host_machine]
system = 'darwin'
subsystem = 'macos'
kernel = 'xnu'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
[properties]
sys_root = '$(SYSROOT)'
pkg_config_libdir = ['$(SYSROOT)/usr/lib/pkgconfig', '$(SYSROOT)/usr/share/pkgconfig']
needs_exe_wrapper = true
[built-in options]
c_args = [$(call pie_meson_list,$(filter-out -Os -g,$(BASE_CFLAGS)))]
c_link_args = [$(call pie_meson_list,$(BASE_LDFLAGS))]
cpp_args = [$(call pie_meson_list,$(filter-out -Os -g,$(BASE_CFLAGS)))]
cpp_link_args = [$(call pie_meson_list,$(BASE_LDFLAGS))]
endef

define PIE_CMAKE_TOOLCHAIN_FILE
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_SYSROOT "$(SYSROOT)")
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.5)
set(CMAKE_C_COMPILER "$(CC)")
set(CMAKE_CXX_COMPILER "$(CXX)")
set(CMAKE_AR "$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-ar")
set(CMAKE_RANLIB "$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-ranlib")
set(CMAKE_STRIP "$(STRIP)")
set(CMAKE_NM "$(NM)")
set(CMAKE_INSTALL_NAME_TOOL "$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-install_name_tool")
set(CMAKE_OTOOL "$(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-otool")
set(CMAKE_C_FLAGS_INIT "$(filter-out -Os -g,$(BASE_CFLAGS))")
set(CMAKE_CXX_FLAGS_INIT "$(filter-out -Os -g,$(BASE_CFLAGS))")
set(CMAKE_EXE_LINKER_FLAGS_INIT "$(BASE_LDFLAGS)")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$(BASE_LDFLAGS)")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "$(BASE_LDFLAGS)")
set(CMAKE_FIND_ROOT_PATH "$(SYSROOT)")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(PKG_CONFIG_EXECUTABLE "$(PIE_PKG_CONFIG)")
endef

define PIE_PKG_CONFIG_SH
#!/bin/sh
# pkg-config for the darwin-arm64 target: SYSROOT's .pc files only.
PKG_CONFIG_LIBDIR='$(SYSROOT)/usr/lib/pkgconfig:$(SYSROOT)/usr/share/pkgconfig'
PKG_CONFIG_SYSROOT_DIR='$(SYSROOT)'
export PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
unset PKG_CONFIG_PATH
exec pkgconf "$$@"
endef

build: build-arch build-indep
build-indep:
build-arch: $(W)/stamp-build
$(W)/stamp-build: $(W)/stamp-pie
	rm -rf $(OBJ) $(ROOT)
	$(MAKE) -f debian/rules -j$(JOBS) install
	touch $@

# The sysroot and the cross files. make expands a recipe's $(file ...)
# before it runs the recipe, so the directory is a prerequisite.
$(PIE)/bin:
	rm -rf $(PIE)
	mkdir -p $@
$(W)/stamp-pie: | $(PIE)/bin
ifeq ($(PIE_FULL_SOURCE),)
	$(PIETOOLCHAIN)/pie-source-guard $(SRCROOT)
endif
	rm -rf $(W)/sysroot
ifneq ($(PIE_CROSS),)
	mkdir -p $(W)/sysroot
	cp -as $(SDK)/. $(W)/sysroot/
	set -e; for d in $(PIE_SYSROOT_DIRS); do \
		[ -d "$(PIE_CROSS_ROOT)/$$d" ] || continue; \
		mkdir -p "$(W)/sysroot/$$d"; \
		cp -a --remove-destination "$(PIE_CROSS_ROOT)/$$d/." "$(W)/sysroot/$$d/"; \
	done
endif
	$(file >$(PIE_PKG_CONFIG),$(PIE_PKG_CONFIG_SH))
	chmod 755 $(PIE_PKG_CONFIG)
	$(file >$(MESON_CROSS),$(PIE_MESON_CROSS_INI))
	$(file >$(CMAKE_TOOLCHAIN),$(PIE_CMAKE_TOOLCHAIN_FILE))
	ln -sf $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-install_name_tool $(PIE)/bin/install_name_tool
	ln -sf $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-otool $(PIE)/bin/otool
	touch $@

binary: binary-arch binary-indep
binary-indep:
binary-arch: build-arch
	rm -rf $(foreach p,$(PIE_PACKAGES),$(CURDIR)/debian/$(p))
	mkdir -p $(PKG)
	cp -a $(ROOT)/. $(PKG)/
	set -e; for p in $(wordlist 2,$(words $(PIE_PACKAGES)),$(PIE_PACKAGES)); do \
		[ -f debian/$$p.files ] || { echo "rules.mk: $$p has no debian/$$p.files" >&2; exit 1; }; \
		mkdir -p $(CURDIR)/debian/$$p; \
		sed -e 's/#.*//' -e '/^[[:space:]]*$$/d' debian/$$p.files | while read -r g; do \
			( cd $(PKG); n=0; for f in $$g; do \
				[ -e "$$f" ] || [ -L "$$f" ] || continue; n=1; \
				mkdir -p $(CURDIR)/debian/$$p/$$(dirname "$$f"); \
				mv "$$f" $(CURDIR)/debian/$$p/"$$f"; \
				rmdir -p --ignore-fail-on-non-empty "$$(dirname "$$f")" 2>/dev/null || true; \
			done; \
			[ $$n = 1 ] || { echo "rules.mk: debian/$$p.files: $$g matches nothing" >&2; exit 1; } ) || exit 1; \
		done; \
	done
	set -e; for p in $(PIE_PACKAGES); do \
		d=$(CURDIR)/debian/$$p; mkdir -p $$d/DEBIAN; \
		for x in etc tmp var; do \
			if [ -e $$d/$$x ]; then mkdir -p $$d/private; mv $$d/$$x $$d/private/$$x; fi; \
		done; \
		install -D -m 644 debian/copyright $$d/usr/share/doc/$$p/copyright; \
		if [ -d $$d/private/etc ]; then \
			(cd $$d && find private/etc -type f -printf '/%p\n' | sort > DEBIAN/conffiles); fi; \
		chmod -R go-w $$d; \
		(cd $$d && find . -path ./DEBIAN -prune -o -type f -printf '%P\0' | sort -z | \
			xargs -0 md5sum > DEBIAN/md5sums); \
		dpkg-gencontrol -p$$p -P$$d; \
		find $$d -exec touch -h -d @$(SOURCE_DATE_EPOCH) {} +; \
		dpkg-deb --root-owner-group -Zxz --build $$d ..; \
	done

clean:
	rm -rf $(W) $(foreach p,$(PIE_PACKAGES),$(CURDIR)/debian/$(p)) debian/files debian/*.substvars

.PHONY: build build-indep build-arch binary binary-indep binary-arch clean
