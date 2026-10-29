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
