# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := llvm-runtimes
include ../common.mk

LLVM := $(PIE)/src/llvm-project
BINUTILS := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin

CMAKE_FLAGS := -G Ninja -S $(LLVM)/runtimes -B $(OBJ) \
	-C $(LLVM)/libcxx/cmake/caches/Apple.cmake \
	-DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" \
	-DCMAKE_SYSTEM_NAME=Darwin -DCMAKE_OSX_SYSROOT=$(SDK) \
	-DCMAKE_OSX_ARCHITECTURES=$(ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=26.5 \
	-DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) \
	-DCMAKE_C_FLAGS="-mcpu=generic" -DCMAKE_CXX_FLAGS="-mcpu=generic" \
	-DCMAKE_SHARED_LINKER_FLAGS="-Wl,-oso_prefix,$(PIE)/" \
	-DCMAKE_AR=$(BINUTILS)-ar -DCMAKE_RANLIB=$(BINUTILS)-ranlib \
	-DCMAKE_LIBTOOL=$(BINUTILS)-libtool -DCMAKE_INSTALL_NAME_TOOL=$(BINUTILS)-install_name_tool \
	-DCMAKE_NM=$(BINUTILS)-nm -DCMAKE_OBJDUMP=$(BINUTILS)-otool -DCMAKE_STRIP=$(BINUTILS)-strip \
	-DCMAKE_INSTALL_PREFIX=$(SDK)/usr \
	-DLIBCXX_ENABLE_VENDOR_AVAILABILITY_ANNOTATIONS=OFF -DLIBCXX_PSTL_BACKEND=serial \
	-DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
	-DLIBCXXABI_INCLUDE_TESTS=OFF -DLIBUNWIND_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF

HEADER_FLAGS := -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

$(OBJ)/hdrs.configured:
	cmake $(CMAKE_FLAGS) $(HEADER_FLAGS)
	touch $@

installhdrs: $(OBJ)/hdrs.configured
	ninja -C $(OBJ) install-cxx-headers install-cxxabi-headers install-unwind-headers

UNWIND := $(LLVM)/libunwind
UNWIND_SRCS := libunwind.cpp Unwind-EHABI.cpp Unwind-seh.cpp UnwindLevel1.c \
	UnwindLevel1-gcc-ext.c Unwind-sjlj.c Unwind-wasm.c UnwindRegistersRestore.S \
	UnwindRegistersSave.S
UNWIND_OBJS := $(patsubst %,$(OBJ)/unwind/%.o,$(basename $(UNWIND_SRCS)))
UNWIND_DYLIB := $(OBJ)/libunwind.dylib
UNWIND_FLAGS := $(BASE_CFLAGS) -I$(UNWIND)/include -funwind-tables -D_LIBUNWIND_IS_NATIVE_ONLY

$(OBJ)/unwind/%.o: $(UNWIND)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(UNWIND_FLAGS) -std=c++17 -fno-exceptions -fno-rtti -nostdinc++ -c $< -o $@
$(OBJ)/unwind/%.o: $(UNWIND)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(UNWIND_FLAGS) -std=c99 -fexceptions -c $< -o $@
$(OBJ)/unwind/%.o: $(UNWIND)/src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(UNWIND_FLAGS) -c $< -o $@

$(UNWIND_DYLIB): $(UNWIND_OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib -install_name /usr/lib/system/libunwind.dylib \
		-compatibility_version 1 -current_version 21.1.8 -Wl,-umbrella,System \
		$(UNWIND_OBJS) -L$(SDK)/usr/lib/system -lsystem_c -lsystem_malloc -lsystem_kernel -lsystem_pthread \
		-lsystem_platform -lcompiler_rt -ldyld -o $@

LIBS_OBJ := $(OBJ)-libs
$(LIBS_OBJ)/configured:
	cmake $(subst -B $(OBJ),-B $(LIBS_OBJ),$(CMAKE_FLAGS)) -DLLVM_ENABLE_RUNTIMES="libcxxabi;libcxx" \
		-DCMAKE_CXX_COMPILER_WORKS=ON -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON
	touch $@

CXX_DYLIBS := $(LIBS_OBJ)/lib/libc++.1.0.dylib $(LIBS_OBJ)/lib/libc++abi.1.0.dylib
$(CXX_DYLIBS) &: $(LIBS_OBJ)/configured
	ninja -C $(LIBS_OBJ) cxxabi_shared cxx_shared

install-unwind: $(UNWIND_DYLIB)
	$(call install_both,$(UNWIND_DYLIB),usr/lib/system)

install: install-unwind $(CXX_DYLIBS)
	set -e; for l in c++ c++abi; do \
		for d in $(SDK) $(ROOT); do \
			install -d $$d/usr/lib; \
			install -m 755 $(LIBS_OBJ)/lib/lib$$l.1.0.dylib $$d/usr/lib/lib$$l.1.dylib; \
			ln -sf lib$$l.1.dylib $$d/usr/lib/lib$$l.dylib; \
		done; \
	done

clean:
	rm -rf $(OBJ)

.PHONY: installhdrs install install-unwind clean
