# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := dyld
TAG     := dyld-1378
include ../common.mk

INC := $(SRCROOT)/include

BASE := $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) \
	-fvisibility-inlines-hidden -Xclang -target-feature -Xclang +mte \
	-Wno-vla-extension -Wno-c99-designator -Wno-reorder-init-list
CXXSTD := -std=c++20 -fno-exceptions -fno-rtti
CSTD   := -std=c2x
MACHO_FLAGS := $(BASE) -fvisibility=hidden -DINTERNAL_BUILD=1 \
	-I$(SRCROOT)/dyld -I$(SRCROOT)/common -I$(SRCROOT)/mach_o -I$(SRCROOT)/lsl \
	-isystem $(SRCROOT)/include

MACHO_OBJS := $(patsubst %.cpp,$(OBJ)/mach_o/%.o,$(shell cat mach_o.list))
LIBMACHO   := $(OBJ)/libmach_o.a

$(OBJ)/mach_o/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CXX) $(MACHO_FLAGS) $(CXXSTD) -c $< -o $@

$(LIBMACHO): $(MACHO_OBJS)
	rm -f $@
	$(LIBTOOL) -static -o $@ $^

DYLD_FLAGS := $(BASE) -DDYLD_VERSION=1378 -DBUILDING_DYLD=1 -D_LIBCPP_NO_EXCEPTIONS=1 \
	$(addprefix -I$(SRCROOT)/,dyld common libdyld mach_o lsl libdyld_introspection) \
	$(addprefix -iquote $(SRCROOT)/,cache_builder include/mach-o other-tools) \
	-I$(OBJ)/derived -isystem $(SRCROOT)/include
PBL_STRUCTS := "class dyld4::PrebuiltLoader" "struct dyld4::PrebuiltLoaderSet" \
	"struct dyld4::ObjCBinaryInfo" "union mach_o::LinkedDylibAttributes" \
	"struct dyld4::Loader::DylibPatch" "struct dyld4::Loader::FileValidationInfo" \
	"class dyld3::MapView<struct prebuilt_objc::ObjCStringKeyOnDisk" \
	"class dyld3::MultiMapView<struct prebuilt_objc::ObjCStringKeyOnDisk" \
	"class objc::SelectorHashTable"
$(OBJ)/derived/PrebuiltLoader_version.h: $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	printf '#include "PrebuiltLoader.h"\n#include "PrebuiltObjC.h"\n#include "OptimizerObjC.h"\nint foo() { return sizeof(dyld4::PrebuiltLoader)+sizeof(mach_o::LinkedDylibAttributes)+sizeof(dyld4::PrebuiltLoaderSet)+sizeof(dyld4::ObjCBinaryInfo)+sizeof(dyld4::Loader::DylibPatch)+sizeof(dyld4::Loader::FileValidationInfo)+sizeof(prebuilt_objc::ObjCSelectorMapOnDisk)+sizeof(prebuilt_objc::ObjCObjectMapOnDisk)+sizeof(objc::SelectorHashTable); }\n' > $(OBJ)/derived/test.cpp
	cd $(SRCROOT) && $(CXX) -arch $(ARCH) -isysroot $(SDK) -mmacosx-version-min=13.0 -DBUILDING_DYLD=1 \
		-std=c++2a -w -fsyntax-only -Xclang -fdump-record-layouts -Icommon -Idyld -Iinclude \
		-Icache_builder -Ilsl -Imach_o -Iinclude/mach-o $(OBJ)/derived/test.cpp > $(OBJ)/derived/test.out
	set -e; cd $(OBJ)/derived; : > layouts.ast; \
	for s in $(PBL_STRUCTS); do grep -A100 "$$s" test.out | grep -B100 -m1 sizeof= >> layouts.ast; done; \
	grep -A6 "struct dyld4::PrebuiltLoader::BindTargetRef::Absolute" test.out | grep -B100 -m1 sizeof= >> layouts.ast; \
	md5sum layouts.ast | awk '{print "#define PREBUILTLOADER_VERSION 0x" substr($$1,1,8)}' > $@

DYLD_SRCS := $(shell cat dyld.list)
DYLD_OBJS := $(patsubst %,$(OBJ)/dyld/%.o,$(basename $(DYLD_SRCS))) $(OBJ)/dyld/hash_memory.o

$(OBJ)/dyld/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched $(OBJ)/derived/PrebuiltLoader_version.h
	@mkdir -p $(dir $@)
	$(CXX) $(DYLD_FLAGS) $(CXXSTD) -c $< -o $@
$(OBJ)/dyld/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(DYLD_FLAGS) $(CSTD) -c $< -o $@
$(OBJ)/dyld/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(DYLD_FLAGS) -c $< -o $@
$(OBJ)/dyld/hash_memory.o: hash_memory.cpp $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CXX) $(DYLD_FLAGS) $(CXXSTD) -c $< -o $@

dyld-objs: $(DYLD_OBJS)

DYLD := $(OBJ)/product/dyld
DYLD_ARCHIVES = $(filter-out %compiler_rt% %unwind% %c++abi%,$(wildcard $(SDK)/usr/local/lib/dyld/*.a)) \
	$(SDK)/usr/local/lib/libCrashReporterClient.a $(SDK)/usr/local/lib/libcorecrypto_static.a
$(DYLD): $(DYLD_OBJS) $(LIBMACHO) $(DYLD_ARCHIVES)
	@mkdir -p $(dir $@)
	$(CXX) $(BASE_LDFLAGS) -nostdlib -e __dyld_start \
		-Wl,-exported_symbol,__dyld_start -Wl,-exported_symbol,_lldb_image_notifier \
		-Wl,-exported_symbol,_dyld_all_image_infos -Wl,-no_inits \
		-Wl,-dylinker -Wl,-dylinker_install_name,/usr/lib/dyld \
		-Wl,-fixup_chains -Wl,-data_const -Wl,-dead_strip \
		-Wl,-dirty_data_list,$(SRCROOT)/dyld/dyld.dirty \
		-Wl,-section_order,__TPRO_CONST,__data:__auth_got:__allocator \
		$(DYLD_OBJS) $(LIBMACHO) $(DYLD_ARCHIVES) $(SDK)/usr/local/lib/libclang_rt.osx.a -o $@

PUBLIC_MACHO  := dyld_images.h dyld.h fixup-chains.h utils.h
PRIVATE_MACHO := dyld_cache_format.h dyld_introspection.h dyld_priv.h \
	dyld_process_info.h dyld-interposing.h function-variant-macros.h utils_priv.h

all: installhdrs $(DYLD) $(LIBDYLD)

installhdrs: fetch
	$(call copy_headers,$(addprefix $(INC)/mach-o/,$(PUBLIC_MACHO)),usr/include/mach-o)
	$(call copy_headers,$(addprefix $(INC)/mach-o/,$(PRIVATE_MACHO)),usr/local/include/mach-o)
	$(call copy_headers,$(INC)/dlfcn.h,usr/include)
	$(call copy_headers,$(INC)/dlfcn_private.h $(INC)/objc-shared-cache.h,usr/local/include)
	$(PIE)/userland/tools/dyld-version-defines.py $(SDK)/usr/local/include/mach-o/dyld_version_defines.h \
		$(SDK)/usr/local/include/dyld

LIBDYLD_FLAGS := $(BASE) -DBUILDING_LIBDYLD=1 \
	$(addprefix -I$(SRCROOT)/,dyld common libdyld mach_o lsl libdyld_introspection) \
	$(addprefix -iquote $(SRCROOT)/,cache_builder include/mach-o other-tools) \
	-I$(OBJ)/derived -I$(SRCROOT)/include
LIBDYLD_SRCS := $(shell cat libdyld.list)
LIBDYLD_OBJS := $(patsubst %,$(OBJ)/libdyld/%.o,$(basename $(LIBDYLD_SRCS))) \
	$(OBJ)/libdyld/verbose_abort.o $(OBJ)/libdyld/availability.o $(OBJ)/libdyld/hash_memory.o
LIBDYLD := $(OBJ)/product/libdyld.dylib

$(OBJ)/libdyld/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched $(OBJ)/derived/PrebuiltLoader_version.h
	@mkdir -p $(dir $@)
	$(CXX) $(LIBDYLD_FLAGS) $(CXXSTD) -c $< -o $@
$(OBJ)/libdyld/%.o: $(SRCROOT)/%.s $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(LIBDYLD_FLAGS) -c $< -o $@
$(OBJ)/libdyld/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -c $< -o $@
$(OBJ)/libdyld/hash_memory.o: hash_memory.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(LIBDYLD_FLAGS) $(CXXSTD) -c $< -o $@

$(LIBDYLD): $(LIBDYLD_OBJS) $(LIBMACHO)
	@mkdir -p $(dir $@)
	$(CXX) $(BASE_LDFLAGS) -dynamiclib -nostdlib -install_name /usr/lib/system/libdyld.dylib \
		-compatibility_version 1 -current_version 1378 -Wl,-umbrella,System \
		-Wl,-no_inits -Wl,-dead_strip -Wl,-data_const \
		-Wl,-dirty_data_list,$(SRCROOT)/configs/libdyld.dirty \
		-Wl,-unexported_symbol,'__ZNSt3*' $(LIBDYLD_OBJS) $(LIBMACHO) \
		-L$(SDK)/usr/local/lib -lCrashReporterClient -L$(SDK)/usr/lib/system \
		-Wl,-no_warn_unused_dylibs -Wl,-upward-lsystem_platform -Wl,-upward-lsystem_malloc \
		-Wl,-upward-lsystem_c -Wl,-upward-lsystem_pthread -Wl,-upward-lxpc \
		-Wl,-upward-lsystem_blocks -Wl,-upward-lsystem_kernel -Wl,-upward-ldispatch \
		-Wl,-upward-lcompiler_rt -o $@

install: installhdrs $(DYLD) $(LIBDYLD)
	install -d $(ROOT)/usr/lib
	install -m 755 $(DYLD) $(ROOT)/usr/lib/dyld
	$(call install_both,$(LIBDYLD),usr/lib/system)
	$(call install_xcode_man,$(SRCROOT)/dyld.xcodeproj)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean dyld-objs
