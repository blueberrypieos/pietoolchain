# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#   ROOT  the project's staging root, build/pkgroot/<project directory>:
#         what it installs on the target.
#   OBJ   per-project build directory.

.DEFAULT_GOAL := all

PIE     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
TOOLCHAIN := $(PIE)/toolchain
SDK       := $(TOOLCHAIN)/sdk/MacOSX.platform/Developer/SDKs/MacOSX.sdk
ROOT      ?= $(PIE)/build/pkgroot/$(notdir $(CURDIR))
OBJ       ?= $(PIE)/build/userland/$(PROJECT)
SRCROOT   ?= $(PIE)/build/src/$(PROJECT)

CC        := $(TOOLCHAIN)/bin/clang
CXX       := $(TOOLCHAIN)/bin/clang++
LIBTOOL   := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-libtool -D
MIG       := $(TOOLCHAIN)/prefix/bin/mig
STRIP     := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-strip
NM        := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin-nm
export MIGCC := $(CC)
export PATH := $(TOOLCHAIN)/bin:$(TOOLCHAIN)/prefix/bin:$(PATH)

ARCH      := arm64
ARCHFLAGS := -arch $(ARCH) -mcpu=generic
MINVER    := -mmacosx-version-min=26.5

TARGET_OS_DEFS := -DTARGET_OS_EXCLAVECORE=0 -DTARGET_OS_EXCLAVEKIT=0
BASE_CFLAGS := $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) $(TARGET_OS_DEFS) -Os -g \
	-fno-common -fstack-protector-strong
BASE_LDFLAGS := $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) -Wl,-adhoc_codesign \
	-Wl,-oso_prefix,$(PIE)/

PIE_PROJECT := $(notdir $(CURDIR))
ifneq ($(shell grep -sE '^License:.*APSL-2\.0' $(CURDIR)/copyright),)
PIE_PACKAGE := $(shell awk '/^Package:/ { p = $$2 } /^X-Pie-Project:/ && $$2 == "$(PIE_PROJECT)" { print p }' \
	$(PIE)/debian/control)
SOURCE_NOTICE_DOCS := $(if $(PIE_PACKAGE),'/usr/share/doc/$(PIE_PACKAGE)/copyright (in the SDK:' \
	'/usr/share/doc/pietoolchain-sdk/copyright) names the files.',\
	'/usr/share/doc/pietoolchain-sdk/copyright names the files.')
SOURCE_NOTICE := $(PIE)/build/notices/$(PIE_PROJECT)
SOURCE_NOTICE_TEXT := 'pietoolchain, $(PIE_PROJECT).' \
	'This file contains Covered Code under the Apple Public Source License 2.0.' \
	'Its Source Code is available under the terms of that License from' \
	'https://github.com/blueberrypieos/pietoolchain (userland/$(PIE_PROJECT),' \
	'with the upstream source its Makefile names, in src/);' \
	$(SOURCE_NOTICE_DOCS)
$(shell mkdir -p $(dir $(SOURCE_NOTICE)) && printf '%s\n' $(SOURCE_NOTICE_TEXT) > $(SOURCE_NOTICE).new && \
	{ cmp -s $(SOURCE_NOTICE).new $(SOURCE_NOTICE) && rm -f $(SOURCE_NOTICE).new || \
	mv -f $(SOURCE_NOTICE).new $(SOURCE_NOTICE); })
BASE_LDFLAGS += -Wl,-sectcreate,__TEXT,__source,$(SOURCE_NOTICE)
# The same notice for static archives, as a member that defines no symbol, so
# no link takes it in: __TEXT,__source_notice.
SOURCE_NOTICE_OBJ := $(OBJ)/source-notice.o
$(SOURCE_NOTICE_OBJ): $(SOURCE_NOTICE)
	@mkdir -p $(dir $@)
	printf '\t.section __TEXT,__source_notice\n\t.incbin "%s"\n' $(SOURCE_NOTICE) | \
		$(CC) $(ARCHFLAGS) -c -x assembler -o $@ -
endif

define install_both
	install -d $(SDK)/$(2) $(ROOT)/$(2)
	install -m 755 $(1) $(SDK)/$(2)/
	install -m 755 $(1) $(ROOT)/$(2)/
	rm -f $(addprefix $(SDK)/$(2)/,$(addsuffix .stub,$(notdir $(1))))
endef

include $(PIE)/userland/tools/man.mk

REPO ?= $(PROJECT)
REPO_URL ?= https://github.com/apple-oss-distributions/$(REPO)
VENDOR := $(PIE)/src/$(notdir $(patsubst %.git,%,$(REPO_URL)))
PATCHES := $(sort $(wildcard $(CURDIR)/patches/*.patch))
FETCH_ID := $(TAG) $(notdir $(PATCHES))
ifneq ($(TAG),)
ifneq ($(strip $(shell cat $(SRCROOT)/.fetched 2>/dev/null)),$(strip $(FETCH_ID)))
.PHONY: $(SRCROOT)/.fetched
endif
endif
ifneq ($(TAG),)
$(SRCROOT)/.fetched: $(PATCHES)
	@test -n "$$(ls -A $(VENDOR) 2>/dev/null)" || \
		{ echo "$(PROJECT): no $(VENDOR) (git submodule update --init)" >&2; exit 1; }
	rm -rf $(SRCROOT)
	mkdir -p $(SRCROOT)
	cd $(VENDOR) && tar -c --exclude=./.git . | tar -x -C $(SRCROOT)
	git init -q $(SRCROOT)
	set -e; for p in $(PATCHES); do git -C $(SRCROOT) apply $$p; done
	echo '$(FETCH_ID)' > $@
fetch: $(SRCROOT)/.fetched
endif

#   $(call copy_headers,FILES,SDK-RELATIVE-DIR,UNIFDEF-FLAGS)
define copy_headers
	install -d $(SDK)/$(2)
	set -e; for h in $(1); do \
		t=$(SDK)/$(2)/.$$(basename $$h).new; \
		install -m 644 $$h $$t; \
		if [ -n "$(3)" ]; then rc=0; unifdef $(3) -o $$t $$t || rc=$$?; \
			case $$rc in 0) ;; 1) python3 -I $(PIE)/userland/tools/unifdef-notice.py $$t $(3) ;; \
			*) exit $$rc ;; esac; fi; \
		mv -f $$t $(SDK)/$(2)/$$(basename $$h); \
	done
endef

#   $(call copy_tree,SRC-DIR,SDK-RELATIVE-DIR,EXTRA-FIND-ARGS)
define copy_tree
	install -d $(SDK)/$(2)
	cd $(1) && find . \( -name '*.h' -o -name '*.modulemap' \) $(3) -print | \
		while read f; do install -D -m 644 "$$f" "$(SDK)/$(2)/$$f.new" && \
			mv -f "$(SDK)/$(2)/$$f.new" "$(SDK)/$(2)/$$f"; done
endef
