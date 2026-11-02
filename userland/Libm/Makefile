# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := Libm
TAG     := Libm-2026
include ../common.mk

all: installhdrs

installhdrs: fetch
	install -m 644 $(SRCROOT)/Source/ARM/math.h $(SDK)/usr/include/math.h
	install -m 644 $(SRCROOT)/Source/ARM/fenv.h $(SDK)/usr/include/fenv.h

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs clean
