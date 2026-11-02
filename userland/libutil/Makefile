# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libutil
TAG     := libutil-73
include ../common.mk

C_SRCS   := getmntopts.c humanize_number.c pidfile.c expand_number.c realhostname.c \
	reexec_to_match_kernel.c trimdomain.c tzlink.c tzbootuuid.c
CXX_SRCS := ExtentManager.cpp wipefs.cpp
OBJS     := $(patsubst %,$(OBJ)/%.o,$(basename $(C_SRCS) $(CXX_SRCS)))
DYLIB    := $(OBJ)/libutil.dylib

FLAGS := $(BASE_CFLAGS) -Wall -I$(SRCROOT) -isystem $(SDK)/usr/local/include

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@
$(OBJ)/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched
	@mkdir -p $(OBJ)
	$(CXX) $(FLAGS) -std=gnu++17 -c $< -o $@

$(DYLIB): $(OBJS)
	$(CXX) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libutil.dylib \
		-compatibility_version 1 -current_version 1 \
		-Wl,-exported_symbols_list,$(SRCROOT)/libutil.exports $(OBJS) -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)

installhdrs: fetch
	$(call copy_headers,$(addprefix $(SRCROOT)/,libutil.h mntopts.h tzlink.h wipefs.h),usr/local/include)

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
