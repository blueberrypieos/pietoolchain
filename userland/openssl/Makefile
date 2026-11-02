# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT  := openssl
TAG      := openssl-3.5.9
REPO_URL := https://github.com/openssl/openssl
include ../common.mk

BINUTILS := $(TOOLCHAIN)/prefix/bin/aarch64-apple-darwin
TESTOBJ  := $(OBJ)-tests

OSSL_OPTIONS := darwin64-arm64 --prefix=/usr --openssldir=/etc/ssl --libdir=lib shared \
	no-engine no-legacy no-fips no-module no-docs
REL := $(shell realpath -m --relative-to=$(OBJ) $(PIE))
OSSL_ENV := CC=$(REL)/userland/openssl/cc-asm AR=$(BINUTILS)-ar RANLIB='$(BINUTILS)-ranlib -c' \
	CFLAGS='$(subst $(PIE)/,$(REL)/,$(filter-out -Os,$(BASE_CFLAGS))) -O2 -Wall' LDFLAGS='$(BASE_LDFLAGS)'

MAN_VARS := MANDOCS3= MANDOCS7=

all: $(OBJ)/.built

$(OBJ)/.configured: $(SRCROOT)/.fetched cc-asm Makefile
	rm -rf $(OBJ)
	mkdir -p $(OBJ)
	cd $(OBJ) && $(OSSL_ENV) perl $(SRCROOT)/Configure $(OSSL_OPTIONS) no-tests > configure.log
	touch $@

$(OBJ)/.built: $(OBJ)/.configured
	$(MAKE) -C $(OBJ) build_sw
	$(MAKE) -C $(OBJ) $(MAN_VARS) build_man_docs
	touch $@

check-isa: $(OBJ)/.built
	./check-isa llvm-objdump-21 $(OBJ)

install: all check-isa
	rm -rf $(OBJ)/dest
	$(MAKE) -C $(OBJ) DESTDIR=$(OBJ)/dest $(MAN_VARS) install_sw install_ssldirs install_man_docs > $(OBJ)/install.log
	set -e; for d in $(SDK) $(ROOT); do \
		install -d $$d/usr/lib; \
		for l in crypto ssl; do \
			install -m 755 $(OBJ)/dest/usr/lib/lib$$l.3.dylib $$d/usr/lib/; \
			ln -sf lib$$l.3.dylib $$d/usr/lib/lib$$l.dylib; \
		done; \
	done
	$(call copy_tree,$(OBJ)/dest/usr/include/openssl,usr/include/openssl)
	install -d $(SDK)/usr/lib/pkgconfig
	install -m 644 $(OBJ)/dest/usr/lib/pkgconfig/*.pc $(SDK)/usr/lib/pkgconfig/
	install -d $(ROOT)/usr/bin $(ROOT)/etc/ssl/certs
	install -d -m 700 $(ROOT)/etc/ssl/private
	install -m 755 $(OBJ)/dest/usr/bin/openssl $(ROOT)/usr/bin/
	install -m 644 $(OBJ)/dest/etc/ssl/openssl.cnf $(OBJ)/dest/etc/ssl/ct_log_list.cnf $(ROOT)/etc/ssl/
	install -d $(ROOT)/usr/share/man
	cp -R $(OBJ)/dest/usr/share/man/man1 $(OBJ)/dest/usr/share/man/man5 $(ROOT)/usr/share/man/

SSL_TESTS := $(notdir $(basename $(wildcard $(SRCROOT)/test/ssl-tests/*.cnf.in)))

kat: $(TESTOBJ)/.built

$(TESTOBJ)/.configured: $(SRCROOT)/.fetched cc-asm Makefile
	rm -rf $(TESTOBJ)
	mkdir -p $(TESTOBJ)
	cd $(TESTOBJ) && $(OSSL_ENV) perl $(SRCROOT)/Configure $(OSSL_OPTIONS) > configure.log
	touch $@

$(TESTOBJ)/.built: $(TESTOBJ)/.configured
	$(MAKE) -C $(TESTOBJ) build_generated
	$(MAKE) -C $(TESTOBJ) test/evp_test test/ssl_test
	rm -rf $(TESTOBJ)/ssl-tests
	mkdir -p $(TESTOBJ)/ssl-tests
	set -e; for t in $(SSL_TESTS); do \
		SRCTOP=$(SRCROOT) BLDTOP=$(TESTOBJ) perl -I $(SRCROOT)/util/perl \
			$(SRCROOT)/test/generate_ssl_tests.pl $(SRCROOT)/test/ssl-tests/$$t.in default \
			> $(TESTOBJ)/ssl-tests/$$t; \
	done
	touch $@

clean:
	rm -rf $(OBJ) $(TESTOBJ)

.PHONY: all check-isa install kat clean
