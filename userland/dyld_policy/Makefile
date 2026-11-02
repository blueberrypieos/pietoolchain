# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := dyld_policy
include ../common.mk

LIB := $(OBJ)/libdyld_policy.a

all: $(LIB)

$(OBJ)/policy.o: policy.c
	@mkdir -p $(OBJ)
	$(CC) $(filter-out -fstack-protector-strong,$(BASE_CFLAGS)) -fno-stack-protector \
		-std=gnu11 -Wall -Wextra -c $< -o $@

$(LIB): $(OBJ)/policy.o
	rm -f $@
	$(LIBTOOL) -static -o $@ $^

install: $(LIB)
	install -d $(SDK)/usr/local/lib/dyld
	install -m 644 $(LIB) $(SDK)/usr/local/lib/dyld/

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
