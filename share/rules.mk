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
# The package is then built with:
#
#   dpkg-buildpackage -a darwin-arm64 -us -uc
#
# The recipe can use CC, CXX, LIBTOOL, STRIP, NM, BASE_CFLAGS and
# BASE_LDFLAGS; SDK, the read-only SDK from pietoolchain-sdk; OBJ and ROOT,
# the object and install directories; and PIETOOLCHAIN, the directory that
# holds this file, tools.mk and the project tools.
#
# The build target runs the recipe's install target into debian/build/root.
# The binary target packages that root with Darwin's file system layout:
# /etc, /tmp and /var move under /private, the files in /etc become
# conffiles, debian/copyright is installed in /usr/share/doc, and every
# file is owned by root. Only one binary package is supported: the one
# debian/control names.
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
OBJ           := $(W)/obj
ROOT          := $(W)/root

PIE_PACKAGE := $(shell sed -n 's/^Package: *//p' debian/control)
ifneq ($(words $(PIE_PACKAGE)),1)
$(error rules.mk: debian/control has packages "$(PIE_PACKAGE)", not one)
endif
PKG  := $(CURDIR)/debian/$(PIE_PACKAGE)
JOBS := $(or $(patsubst parallel=%,%,$(filter parallel=%,$(DEB_BUILD_OPTIONS))),1)

CC      := $(TOOLCHAIN)/bin/clang
CXX     := $(TOOLCHAIN)/bin/clang++
LIBTOOL := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-libtool
STRIP   := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-strip
NM      := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-nm
export PATH := $(TOOLCHAIN)/bin:$(TOOLCHAIN)/prefix/bin:$(PATH)

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
BASE_CFLAGS  := $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) $(TARGET_OS_DEFS) -Os -g \
	-fno-common -fstack-protector-strong
BASE_LDFLAGS := $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) -Wl,-adhoc_codesign \
	-Wl,-oso_prefix,$(CURDIR)/

build: build-arch build-indep
build-indep:
build-arch: $(W)/stamp-build
$(W)/stamp-build:
	rm -rf $(OBJ) $(ROOT)
	mkdir -p $(W)
	$(MAKE) -f debian/rules -j$(JOBS) install
	touch $@

binary: binary-arch binary-indep
binary-indep:
binary-arch: build-arch
	rm -rf $(PKG)
	mkdir -p $(PKG)/DEBIAN
	cp -a $(ROOT)/. $(PKG)/
	set -e; for d in etc tmp var; do \
		if [ -e $(PKG)/$$d ]; then mkdir -p $(PKG)/private; mv $(PKG)/$$d $(PKG)/private/$$d; fi; \
	done
	install -D -m 644 debian/copyright $(PKG)/usr/share/doc/$(PIE_PACKAGE)/copyright
	if [ -d $(PKG)/private/etc ]; then \
		cd $(PKG) && find private/etc -type f -printf '/%p\n' | sort > DEBIAN/conffiles; fi
	chmod -R go-w $(PKG)
	cd $(PKG) && find . -path ./DEBIAN -prune -o -type f -printf '%P\0' | sort -z | \
		xargs -0 md5sum > DEBIAN/md5sums
	dpkg-gencontrol -p$(PIE_PACKAGE) -P$(PKG)
	find $(PKG) -exec touch -h -d @$(SOURCE_DATE_EPOCH) {} +
	dpkg-deb --root-owner-group -Zxz --build $(PKG) ..

clean:
	rm -rf $(W) $(PKG) debian/files debian/*.substvars

.PHONY: build build-indep build-arch binary binary-indep binary-arch clean
