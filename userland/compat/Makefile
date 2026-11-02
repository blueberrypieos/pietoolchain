# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := compat
include ../common.mk

HEADERS := $(shell cd include && find . -name '*.h')

all:

installhdrs:
	set -e; for h in $(HEADERS); do install -D -m 644 include/$$h $(SDK)/usr/include/$$h; done

install: installhdrs

clean:

.PHONY: all install installhdrs clean
