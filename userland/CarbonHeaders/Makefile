# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := CarbonHeaders
TAG     := CarbonHeaders-18.1
include ../common.mk

all: installhdrs

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/,MacTypes.h ConditionalMacros.h AssertMacros.h),usr/include)

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs clean
