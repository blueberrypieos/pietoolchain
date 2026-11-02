# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := compiler-rt
include ../common.mk

SRC := $(PIE)/src/llvm-project/compiler-rt/lib/builtins

GENERIC := absvdi2 absvsi2 absvti2 adddf3 addsf3 addvdi3 addvsi3 addvti3 ashldi3 ashlti3 \
	ashrdi3 ashrti3 bswapdi2 bswapsi2 clzdi2 clzsi2 clzti2 cmpdi2 cmpti2 comparedf2 \
	comparesf2 ctzdi2 ctzsi2 ctzti2 divdc3 divdf3 divdi3 divmoddi4 divmodsi4 divmodti4 \
	divsc3 divsf3 divsi3 divti3 extendsfdf2 extendhfsf2 ffsdi2 ffssi2 ffsti2 fixdfdi \
	fixdfsi fixdfti fixsfdi fixsfsi fixsfti fixunsdfdi fixunsdfsi fixunsdfti fixunssfdi \
	fixunssfsi fixunssfti floatdidf floatdisf floatsidf floatsisf floattidf floattisf \
	floatundidf floatundisf floatunsidf floatunsisf floatuntidf floatuntisf int_util \
	lshrdi3 lshrti3 moddi3 modsi3 modti3 muldc3 muldf3 muldi3 mulodi4 mulosi4 muloti4 \
	mulsc3 mulsf3 multi3 mulvdi3 mulvsi3 mulvti3 negdf2 negdi2 negsf2 negti2 negvdi2 \
	negvsi2 negvti2 os_version_check paritydi2 paritysi2 parityti2 popcountdi2 \
	popcountsi2 popcountti2 powidf2 powisf2 subdf3 subsf3 subvdi3 subvsi3 subvti3 \
	truncdfhf2 truncdfsf2 truncsfhf2 ucmpdi2 ucmpti2 udivdi3 udivmoddi4 udivmodsi4 \
	udivmodti4 udivsi3 udivti3 umoddi3 umodsi3 umodti3
APPLE := atomic_flag_clear atomic_flag_clear_explicit atomic_flag_test_and_set \
	atomic_flag_test_and_set_explicit atomic_signal_fence atomic_thread_fence atomic \
	gcc_personality_v0 clear_cache enable_execute_stack eprintf
AARCH64 := aarch64/fp_mode cpu_model/aarch64

OBJS  := $(patsubst %,$(OBJ)/%.o,$(GENERIC) $(APPLE) $(AARCH64)) $(OBJ)/chkstk_darwin.o
DYLIB := $(OBJ)/libcompiler_rt.dylib

CFLAGS := $(BASE_CFLAGS) -std=c11 -fno-builtin -fomit-frame-pointer -I$(SRC)

all: $(DYLIB)

$(OBJ)/%.o: $(SRC)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ)/chkstk_darwin.o: chkstk_darwin.S
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libcompiler_rt.dylib \
		-compatibility_version 1 -current_version 21.1.8 -Wl,-umbrella,System \
		$(OBJS) -L$(SDK)/usr/lib/system -lsystem_kernel -lsystem_platform -lunwind \
		-Wl,-upward-lsystem_malloc \
		-Wl,-upward-lsystem_c -Wl,-upward-ldyld -Wl,-upward-ldispatch -o $@

STATIC := $(OBJ)/libclang_rt.osx.a
$(STATIC): $(OBJS)
	rm -f $@
	$(LIBTOOL) -static -o $@ $^

install: $(DYLIB) $(STATIC)
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(SDK)/usr/local/lib
	install -m 644 $(STATIC) $(SDK)/usr/local/lib/

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
