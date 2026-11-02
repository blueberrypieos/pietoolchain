# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := launchd
TAG     := launchd-842.92.1
include ../common.mk

L := $(SRCROOT)/liblaunch

all: installhdrs

installhdrs: fetch
	$(call copy_headers,$(L)/vproc.h $(L)/launch.h,usr/include)
	$(call copy_headers,$(L)/bootstrap.h,usr/include/servers)
	ln -sf bootstrap.h $(SDK)/usr/include/servers/bootstrap_defs.h
	$(call copy_headers,$(L)/vproc_priv.h $(L)/launch_priv.h $(L)/bootstrap_priv.h \
		$(L)/reboot2.h,usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs clean
