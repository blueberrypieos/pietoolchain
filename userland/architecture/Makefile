# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := architecture
TAG     := architecture-282
include ../common.mk

all: installhdrs

installhdrs: fetch
	set -e; for d in . arm; do \
		for dst in usr/include/architecture \
		    System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders/architecture; do \
			install -d $(SDK)/$$dst/$$d; \
			install -m 644 $(SRCROOT)/$$d/*.h $(SDK)/$$dst/$$d/; \
		done; \
	done

install: installhdrs

clean:

.PHONY: all install installhdrs clean
