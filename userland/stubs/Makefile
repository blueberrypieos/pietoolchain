# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := stubs
include ../common.mk

STUBS := $(basename $(wildcard *.exports))

all install: $(addprefix $(OBJ)/,$(addsuffix .dylib,$(STUBS)))
	set -e; for s in $(STUBS); do \
		dst=$(SDK)/usr/lib/system/$$s.dylib; \
		if [ -e $$dst ] && ! [ -e $$dst.stub ]; then continue; fi; \
		install -m 755 $(OBJ)/$$s.dylib $$dst; touch $$dst.stub; \
	done

$(OBJ)/%.dylib: %.exports $(PIE)/userland/tools/mkstub.py
	mkdir -p $(OBJ)
	$(PIE)/userland/tools/mkstub.py $< $(OBJ)/$*.s
	$(CC) $(BASE_LDFLAGS) -dynamiclib -nostdlib -install_name /usr/lib/system/$*.dylib -compatibility_version 1 \
		-Wl,-umbrella,System $(OBJ)/$*.s -L$(SDK)/usr/lib/system -lsystem_kernel -o $@

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
