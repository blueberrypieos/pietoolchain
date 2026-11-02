# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors
#
# kext.mk: build a kernel extension (kext) for kernels that link kexts when
# they load them, with kxld. A kext's Makefile sets these, then includes it:
#
#   KEXT          its name: the bundle KEXT.kext, executable Contents/MacOS/KEXT
#   KEXT_SOURCES  its C (.c) and C++ (.cpp) sources
#   KEXT_PLIST    its Info.plist (default Info.plist): CFBundleExecutable
#                 KEXT, and OSBundleLibraries naming the symbol sets it calls
#                 the kernel through
#   KEXT_START, KEXT_STOP
#                 its module start and stop functions, if any (C linkage,
#                 kern_return_t f(kmod_info_t *, void *))
#   KEXT_CFLAGS, KEXT_CXXFLAGS, KEXT_LDFLAGS   more flags
#   KEXT_OBJ      the build directory
#   KEXT_SOCS     the kernels it is for (default bcm2711 bcm2712): its
#                 undefined symbols are checked against their symbol sets
#   KERNEL_BUILD  where those kernels were built (KERNEL_BUILD/<soc>/obj), or
#   KEXT_SYSTEM_DIR  their System.kexts (KEXT_SYSTEM_DIR/<soc>/System.kext,
#                 mksystemkext.sh), as a kernel's -dev package has them
#   KEXT_LIBDIRS  directories of other kexts it names as libraries
#   KEXT_INFO     the source of its kmod_info (default kextinfo.c)
#   KEXT_SDK      the kernel SDK (default PIE_SDKROOT)
#
# Targets: all (KEXT_OBJ/KEXT.kext, built and checked with kextcheck.py),
# clean. A kext links nothing but libcc_kext.

KEXT_KERNEL    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KEXT_TOOLCHAIN := $(abspath $(KEXT_KERNEL)/..)

ifeq ($(KEXT),)
$(error kext.mk: set KEXT)
endif
KEXT_PLIST   ?= Info.plist
KEXT_INFO    ?= $(KEXT_KERNEL)/kextinfo.c
KEXT_SOCS    ?= bcm2711 bcm2712
KEXT_SDK     ?= $(PIE_SDKROOT)
ifeq ($(KEXT_OBJ),)
$(error kext.mk: set KEXT_OBJ)
endif
ifeq ($(KEXT_SDK),)
$(error kext.mk: set KEXT_SDK or PIE_SDKROOT)
endif
KEXT_KF  := $(KEXT_SDK)/System/Library/Frameworks/Kernel.framework
KEXT_CC  := $(KEXT_TOOLCHAIN)/bin/clang
KEXT_CXX := $(KEXT_TOOLCHAIN)/bin/clang++

KEXT_ARCHFLAGS := -arch arm64 -mcpu=generic -isysroot $(KEXT_SDK) -mmacosx-version-min=26.0
KEXT_COMMON := $(KEXT_ARCHFLAGS) -Os -g -mkernel -nostdinc -fno-common \
	-DKERNEL=1 -DKERNEL_PRIVATE=1 -DPRIVATE=1 -DAPPLE -DNeXT \
	-I$(KEXT_KF)/PrivateHeaders -I$(KEXT_KF)/Headers \
	-idirafter $(KEXT_SDK)/usr/local/include/kernel -idirafter $(KEXT_SDK)/usr/include
KEXT_C_FLAGS := -std=gnu11
KEXT_CXX_FLAGS := -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions \
	-fno-register-global-dtors-with-atexit -mllvm -disable-atexit-based-global-dtor-lowering
KEXT_C_QUIRKS := -Wno-framework-include-private-from-public
KEXT_CXX_QUIRKS := $(KEXT_C_QUIRKS) -Wno-c99-designator -Wno-reorder-init-list \
	-Wno-missing-designated-field-initializers
KEXT_LINK := $(KEXT_ARCHFLAGS) -mkernel -nostdlib -Xlinker -kext

KEXT_PLIST_GET = $(shell python3 -c 'import plistlib, sys; print(plistlib.load(open(sys.argv[1], "rb")).get(sys.argv[2], ""))' $(KEXT_PLIST) $(1))
KEXT_ID      := $(call KEXT_PLIST_GET,CFBundleIdentifier)
KEXT_VERSION := $(call KEXT_PLIST_GET,CFBundleVersion)

KEXT_BUNDLE := $(KEXT_OBJ)/$(KEXT).kext
KEXT_EXE    := $(KEXT_BUNDLE)/Contents/MacOS/$(KEXT)
KEXT_OBJS   := $(foreach s,$(KEXT_SOURCES),$(KEXT_OBJ)/$(basename $(notdir $(s))).o) $(KEXT_OBJ)/kextinfo.o

kext_uc = $(shell echo $(1) | tr a-z A-Z)
ifeq ($(KEXT_SYSTEM_DIR),)
KEXT_SYSTEMS := $(foreach soc,$(KEXT_SOCS),$(KEXT_OBJ)/system/$(soc)/System.kext)
else
KEXT_SYSTEMS := $(foreach soc,$(KEXT_SOCS),$(KEXT_SYSTEM_DIR)/$(soc)/System.kext)
endif

all: $(KEXT_OBJ)/.checked

define kext_compile_rule
$(KEXT_OBJ)/$(basename $(notdir $(1))).o: $(1) $(MAKEFILE_LIST)
	@mkdir -p $$(@D)
	$$(if $$(filter %.c,$(1)),$(KEXT_CC) $(KEXT_COMMON) $(KEXT_C_FLAGS) $$(KEXT_CFLAGS) $(KEXT_C_QUIRKS),$(KEXT_CXX) $(KEXT_COMMON) -x c++ $(KEXT_CXX_FLAGS) $$(KEXT_CXXFLAGS) $(KEXT_CXX_QUIRKS)) -MMD -MP -c $$< -o $$@
endef
$(foreach s,$(KEXT_SOURCES),$(eval $(call kext_compile_rule,$(s))))
-include $(KEXT_OBJS:.o=.d)

$(KEXT_OBJ)/kextinfo.o: $(KEXT_INFO) $(KEXT_PLIST) $(MAKEFILE_LIST)
	@mkdir -p $(@D)
	$(KEXT_CC) $(KEXT_COMMON) $(KEXT_C_FLAGS) $(KEXT_C_QUIRKS) -DKEXT_ID='"$(KEXT_ID)"' -DKEXT_VERSION='"$(KEXT_VERSION)"' \
		$(if $(KEXT_START),-DKEXT_START=$(KEXT_START)) $(if $(KEXT_STOP),-DKEXT_STOP=$(KEXT_STOP)) \
		-c $< -o $@

$(KEXT_EXE): $(KEXT_OBJS)
	@mkdir -p $(@D)
	$(KEXT_CC) $(KEXT_LINK) $(KEXT_LDFLAGS) $(KEXT_OBJS) -lcc_kext -o $@

$(KEXT_BUNDLE)/Contents/Info.plist: $(KEXT_PLIST)
	@mkdir -p $(@D)
	install -m 644 $< $@

.SECONDEXPANSION:
$(KEXT_OBJ)/system/%/System.kext/Info.plist: $(KEXT_KERNEL)/mksystemkext.sh \
		$$(wildcard $(KERNEL_BUILD)/$$*/obj/DEVELOPMENT_ARM64_$$(call kext_uc,$$*)/config/*.symbolset)
	$(KEXT_KERNEL)/mksystemkext.sh $(KERNEL_BUILD)/$*/obj/DEVELOPMENT_ARM64_$(call kext_uc,$*)/config $(KEXT_OBJ)/system/$*

$(KEXT_OBJ)/.checked: $(KEXT_EXE) $(KEXT_BUNDLE)/Contents/Info.plist $(addsuffix /Info.plist,$(KEXT_SYSTEMS)) \
		$(KEXT_KERNEL)/kextcheck.py
	python3 $(KEXT_KERNEL)/kextcheck.py $(addprefix -L ,$(KEXT_LIBDIRS)) \
		$(KEXT_EXE) $(KEXT_BUNDLE)/Contents/Info.plist $(KEXT_SYSTEMS)
	@touch $@

clean:
	rm -rf $(KEXT_OBJ)

.PHONY: all clean
