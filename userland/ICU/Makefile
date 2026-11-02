# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := ICU
TAG     := ICU-76142.5.1.200
include ../common.mk

SRC    := $(SRCROOT)/icu/icu4c/source
HOST   := $(OBJ)/host
TARGET := $(OBJ)/target
HOSTCC ?= clang-21
HOSTCXX ?= clang++-21
ICU_VERS := 78

APPLE_DEFS := -DU_SHOW_CPLUSPLUS_API=1 -DU_SHOW_INTERNAL_API=1 -DUCONFIG_NO_MF2=1
TARGET_FLAGS := $(ARCHFLAGS) -isysroot $(SDK) $(MINVER) $(TARGET_OS_DEFS) $(APPLE_DEFS) \
	-DU_TIMEZONE=timezone -DICU_DATA_DIR=\\\"/usr/share/icu\\\" -g -Os -fno-exceptions -fvisibility=hidden
CONFIG_FLAGS := --disable-renaming --disable-extras --disable-layout --disable-layoutex \
	--disable-samples --disable-tests --with-data-packaging=archive

DYLIB := $(OBJ)/libicucore.A.dylib
DATA  := $(HOST)/data/out/icudt$(ICU_VERS)l.dat

all: $(DYLIB) $(DATA)

$(HOST)/.built: $(SRCROOT)/.fetched
	rm -rf $(HOST)
	mkdir -p $(HOST)
	cd $(HOST) && CC=$(HOSTCC) CXX=$(HOSTCXX) CFLAGS="$(APPLE_DEFS) -O2" \
		CXXFLAGS="$(APPLE_DEFS) -O2 -std=c++17" $(SRC)/configure $(CONFIG_FLAGS) > configure.log
	env -u MAKEFLAGS $(MAKE) -j$(shell nproc) -C $(HOST) > $(HOST)/make.log
	touch $@
$(DATA): $(HOST)/.built

$(TARGET)/.built: $(HOST)/.built printvar.mk
	rm -rf $(TARGET)
	mkdir -p $(TARGET)
	cd $(TARGET) && CC=$(CC) CXX=$(CXX) CFLAGS="$(TARGET_FLAGS)" \
		CXXFLAGS="$(TARGET_FLAGS) -std=c++17 -fvisibility-inlines-hidden" \
		LDFLAGS="$(ARCHFLAGS) -isysroot $(SDK) $(MINVER)" \
		$(SRC)/configure --host=aarch64-apple-darwin --build=$(shell uname -m)-pc-linux-gnu \
		--with-cross-build=$(HOST) --disable-tools $(CONFIG_FLAGS) > configure.log
	cp $(SRC)/common/Makefile.local $(TARGET)/common/
	cp $(SRC)/i18n/Makefile.local $(TARGET)/i18n/
	set -e; for d in stubdata common i18n io; do \
		objs=$$(env -u MAKEFLAGS $(MAKE) -s -C $(TARGET)/$$d -f Makefile -f $(CURDIR)/printvar.mk \
			print-OBJECTS | tr ' ' '\n' | grep '\.o$$'); \
		env -u MAKEFLAGS $(MAKE) -j$(shell nproc) -C $(TARGET)/$$d $$objs > $(TARGET)/$$d.log; \
		for o in $$objs; do echo $(TARGET)/$$d/$$o; done >> $(TARGET)/objects.list; \
	done
	touch $@

$(DYLIB): $(TARGET)/.built
	$(CXX) $(BASE_LDFLAGS) -dynamiclib -current_version $(ICU_VERS).1 -compatibility_version 1 \
		-Wl,-dead_strip -Wl,-unexported_symbols_list,$(SRC)/common/common_weakexterns.unexports \
		-Wl,-unexported_symbols_list,$(SRC)/i18n/i18n_weakexterns.unexports \
		-install_name /usr/lib/libicucore.A.dylib -o $@ -filelist $(TARGET)/objects.list

install: all installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libicucore.A.dylib $(SDK)/usr/lib/libicucore.dylib
	ln -sf libicucore.A.dylib $(ROOT)/usr/lib/libicucore.dylib
	install -d $(ROOT)/usr/share/icu
	install -m 444 $(DATA) $(ROOT)/usr/share/icu/

installhdrs: fetch
	$(call copy_headers,$(wildcard $(SRC)/common/unicode/*.h $(SRC)/i18n/unicode/*.h \
		$(SRC)/io/unicode/*.h),usr/local/include/unicode)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
