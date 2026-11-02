# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := CrashReporterClient
include ../common.mk

LIB := $(OBJ)/libCrashReporterClient.a

all: $(LIB)

$(OBJ)/crashreporter.o: crashreporter.c
	@mkdir -p $(OBJ)
	$(CC) $(BASE_CFLAGS) -std=gnu11 -Wall -c $< -o $@

$(LIB): $(OBJ)/crashreporter.o
	rm -f $@
	$(LIBTOOL) -static -o $@ $^

install: $(LIB)
	install -d $(SDK)/usr/local/lib
	install -m 644 $(LIB) $(SDK)/usr/local/lib/

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
