# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := CommonCrypto
TAG     := CommonCrypto-600035
PKG_REVISION := 1
include ../common.mk

S := $(SRCROOT)
SOURCES := lib/CommonDigest.c lib/CommonHMAC.c lib/CommonKeyDerivation.c lib/CommonRandom.c \
	lib/ccGlobals.c lib/ccDispatch.c lib/CommonCryptor.c lib/corecryptoSymmetricBridge.c \
	lib/CommonCryptorDES.c lib/CommonCryptorGCM.c lib/CommonCryptorChaCha20.c \
	lib/CommonCryptorChaCha20Poly1305.c lib/CommonCMAC.c lib/CommonSymmetricKeywrap.c \
	lib/CommonKeyDerivationSPI.c lib/CommonRSACryptor.c lib/CommonECCryptor.c lib/CommonDH.c \
	lib/CommonBigNum.c \
	$(addprefix libcn/,adler32.c crc16-a.c crc16-b.c crc16-ccitt-false.c crc16-ccitt-true.c \
	crc16-dect-r.c crc16-dect-x.c crc16-icode.c crc16-usb.c crc16-verifone.c crc16-xmodem.c \
	crc16.c crc32-bzip2.c crc32-castagnoli.c crc32-mpeg-2.c crc32-posix.c crc32-xfer.c crc32.c \
	crc64-ecma-182.c crc8-icode.c crc8-itu.c crc8-rohc.c crc8-wcdma.c crc8.c gen_std_crc_table.c \
	normal_crc.c reflect.c reverse_crc.c reverse_poly.c CommonCRC.c CommonBaseXX.c \
	CommonBuffering.c)
OBJS  := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
OBJS += $(OBJ)/collab-stubs.o
DYLIB := $(OBJ)/libcommonCrypto.dylib

$(OBJ)/collab-stubs.o: collab-stubs.c $(S)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

FLAGS := $(BASE_CFLAGS) -std=gnu11 -Werror=vla -I$(S)/lib -I$(S)/libcn \
	-isystem $(SDK)/usr/local/include \
	-isystem $(SDK)/System/Library/Frameworks/System.framework/PrivateHeaders

all: $(DYLIB)

$(OBJ)/%.o: $(S)/%.c $(S)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(OBJ)/exports.txt: $(S)/.fetched
	@mkdir -p $(dir $@)
	sed 's/[[:space:]]*$$//' $(S)/exports.exp-in | grep . > $@

$(DYLIB): $(OBJS) $(OBJ)/exports.txt
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib \
		-install_name /usr/lib/system/libcommonCrypto.dylib \
		-compatibility_version 1 -current_version 600035 -Wl,-umbrella,System \
		-Wl,-exported_symbols_list,$(OBJ)/exports.txt $(OBJS) \
		-L$(SDK)/usr/lib/system -ldyld -lcompiler_rt -lsystem_kernel -lsystem_platform \
		-lsystem_malloc -lsystem_c -lsystem_blocks -ldispatch -lsystem_asl -lcorecrypto \
		-lsystem_trace -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib/system)
	install -d $(ROOT)/usr/lib/blueberry-pie-os
	sha256sum $(DYLIB) | cut -d' ' -f1 > $(ROOT)/usr/lib/blueberry-pie-os/commoncrypto-ckg-stubs.sha256
	$(call install_xcode_man,$(SRCROOT)/CommonCrypto.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(wildcard $(S)/include/*.h),usr/include/CommonCrypto)
	$(call copy_headers,$(filter-out %/CommonBaseXX.h %/CommonCRC.h %/CommonNumerics.h,\
		$(wildcard $(S)/include/Private/*.h)),usr/local/include/CommonCrypto)
	$(call copy_headers,$(addprefix $(S)/include/Private/,CommonBaseXX.h CommonCRC.h \
		CommonNumerics.h),usr/local/include/CommonNumerics)

# `make regression` builds $(OBJ)/ccregression; run it on the system
# (with test names as arguments to run only those).
# REGRESSION_TESTS lists the tests, in testlistInc.h's names; by default
# all supported cases.
REGRESSION_TESTS ?= CommonRandom CommonCPP CommonCryptoSymECB CommonCryptoSymCBC CommonCryptoSymOFB \
	CommonCryptoSymGCM CommonCryptoSymCCM CommonCryptoSymCTR CommonCryptoSymXTS CommonCryptoSymRC2 \
	CommonCryptoSymRegression CommonCryptoSymOffset CommonCryptoSymZeroLength CommonCryptoOutputLength \
	CommonCryptoNoPad CommonCryptoSymCFB CommonCryptoCTSPadding CommonSymmetricWrap CommonDH CommonDigest \
	CommonHMac CommonCryptoReset CommonCryptoSymChaCha20 CommonCryptoSymChaCha20Poly1305 CommonBigNum \
	CommonBigDigest CommonCryptoWithData CommonCryptoBlowfish CommonCRCTest CommonBaseEncoding \
	CommonHKDF CommonANSIKDF CommonNISTKDF CommonKeyDerivation CommonEC CommonRSA CommonHMacClone \
	CommonCMac
REG := $(OBJ)/ccregression-src
REG_FILE_CommonDH := CommonCrypto/CommonDHtest.c
REG_FILE_CommonCRCTest := CommonNumerics/CommonCRC.c
REG_FILE_CommonBaseEncoding := CommonNumerics/CommonBaseEncoding.c
REG_FILE_CommonCPP := CommonCrypto/CommonCPP.cpp
REG_FILE_CommonSymmetricWrap := CommonCrypto/CommonCryptoSymmetricWrap.c
REG_FILE_CommonCryptoWithData := CommonCrypto/CommonCryptorWithData.c
reg_file = $(REG)/$(or $(REG_FILE_$(1)),CommonCrypto/$(1).c)
REG_SRCS = $(REG)/main.c $(wildcard $(REG)/test/*.c) $(wildcard $(REG)/util/*.c) \
	$(REG)/CommonCrypto/CCCryptorTestFuncs.c $(foreach t,$(REGRESSION_TESTS),$(call reg_file,$(t)))

$(REG)/.copied: $(S)/.fetched Makefile
	rm -rf $(REG)
	cp -R $(S)/CCRegression $(REG)
	printf '%s\n' $(foreach t,$(REGRESSION_TESTS),'ONE_TEST($(t))') > $(REG)/test/testlistInc.h
	mkdir -p $(REG)/shim/Security
	touch $(REG)/shim/Security/Security.h
	touch $@

REG_FLAGS = $(BASE_CFLAGS) -w -I$(REG)/test -I$(REG)/util -I$(REG)/CommonCrypto -I$(S)/include \
	-I$(S)/lib -I$(S)/libcn -I$(REG)/shim \
	-isystem $(SDK)/usr/local/include

$(OBJ)/ccregression: $(REG)/.copied $(DYLIB)
	$(if $(filter %.cpp,$(REG_SRCS)),$(CXX) $(REG_FLAGS) -std=c++17 -c $(filter %.cpp,$(REG_SRCS)) -o $(REG)/cpp.o)
	$(CC) $(REG_FLAGS) $(BASE_LDFLAGS) $(filter %.c,$(REG_SRCS)) $(if $(filter %.cpp,$(REG_SRCS)),$(REG)/cpp.o -lc++) \
		-L$(SDK)/usr/lib -lSystem -o $@

regression: $(OBJ)/ccregression

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs regression clean
