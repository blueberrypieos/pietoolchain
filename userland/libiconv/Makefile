# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libiconv
TAG     := libiconv-115.120.2
include ../common.mk

XCODEPROJ := $(SRCROOT)/libiconv.xcodeproj
sources = $(filter %.c,$(shell python3 ../tools/pbxproj-sources.py $(XCODEPROJ) $(1) 2>/dev/null))

FLAGS     := $(BASE_CFLAGS) -std=gnu89 -DHAVE_CONFIG_H -I$(SRCROOT)/citrus -I$(SRCROOT)/libcharset \
	$(patsubst %/,-I%,$(wildcard $(SRCROOT)/libiconv_modules/*/)) \
	-Wno-deprecated-non-prototype -Wno-gcc-compat
LIB_FLAGS := $(FLAGS) -DBUILDING_DLL -DENABLE_RELOCATABLE -DIN_LIBRARY -DNO_XMALLOC -DPIC
ICONV_FLAGS := $(LIB_FLAGS) -DBUILDING_LIBICONV -Drelocate=libiconv_relocate \
	-Dset_relocation_prefix=libiconv_set_relocation_prefix \
	'-D_PATH_I18NMODULE="/usr/lib/i18n"' '-D_PATH_ESDB="/usr/share/i18n/esdb"' \
	'-D_PATH_CSMAPPER="/usr/share/i18n/csmapper"'

MODULES := BIG5 DECHanyu DECKanji EUC EUCTW GBK2K HZ ISO2022 JOHAB MSKanji UES UTF1632 UTF7 \
	UTF8 UTF8MAC VIQR ZW iconv_none iconv_std mapper_646 mapper_none mapper_parallel \
	mapper_serial mapper_std mapper_zone

CHARSET := $(OBJ)/libcharset.1.dylib
LIBICONV := $(OBJ)/libiconv.2.dylib
MODULE_LIBS := $(patsubst %,$(OBJ)/i18n/lib%.dylib,$(MODULES))

all: $(CHARSET) $(LIBICONV) $(MODULE_LIBS) $(OBJ)/bin/iconv

$(OBJ)/charset/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(LIB_FLAGS) -c $< -o $@
$(OBJ)/iconv/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(ICONV_FLAGS) -c $< -o $@
$(OBJ)/modules/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(LIB_FLAGS) -c $< -o $@
$(OBJ)/tool/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -DDEPENDS_ON_LIBICONV -c $< -o $@

$(CHARSET): $(patsubst %.c,$(OBJ)/charset/%.o,$(call sources,charset))
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libcharset.1.dylib \
		-compatibility_version 1 -current_version 1 $^ -o $@

$(LIBICONV): $(patsubst %.c,$(OBJ)/iconv/%.o,$(call sources,libiconv)) $(CHARSET)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libiconv.2.dylib \
		-compatibility_version 7 -current_version 7 \
		$(filter %.o,$^) -L$(OBJ) -Wl,-reexport-lcharset.1 -o $@

LINK_ICONV := $(LIBICONV) -Wl,-dylib_file,/usr/lib/libcharset.1.dylib:$(CHARSET)

define module
$(OBJ)/i18n/lib$(1).dylib: $(patsubst %.c,$(OBJ)/modules/%.o,$(call sources,$(1))) $(LIBICONV)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/i18n/lib$(1).dylib \
		-compatibility_version 1 -current_version 1 $$(filter %.o,$$^) \
		$(if $(filter iconv_none,$(1)),,$(LINK_ICONV)) -o $$@
endef
$(foreach m,$(MODULES),$(eval $(call module,$(m))))

$(OBJ)/bin/iconv: $(patsubst %.c,$(OBJ)/tool/%.o,$(call sources,iconv)) $(LIBICONV)
	@mkdir -p $(dir $@)
	$(CC) $(BASE_LDFLAGS) $(filter %.o,$^) $(LINK_ICONV) -o $@

install: all installhdrs
	$(call install_both,$(CHARSET) $(LIBICONV),usr/lib)
	set -e; for l in iconv.2 charset.1; do \
		ln -sf lib$$l.dylib $(SDK)/usr/lib/lib$${l%.*}.dylib; \
		ln -sf lib$$l.dylib $(ROOT)/usr/lib/lib$${l%.*}.dylib; \
	done
	install -d $(ROOT)/usr/lib/i18n $(ROOT)/usr/bin
	install -m 755 $(MODULE_LIBS) $(ROOT)/usr/lib/i18n/
	install -m 755 $(OBJ)/bin/iconv $(ROOT)/usr/bin/
	$(call install_xcode_man,$(SRCROOT)/libiconv.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/citrus/iconv.h $(SRCROOT)/libcharset/libcharset.h \
		$(SRCROOT)/libcharset/localcharset.h,usr/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
