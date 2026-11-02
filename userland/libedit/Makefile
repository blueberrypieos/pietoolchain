# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libedit
TAG     := libedit-65
include ../common.mk

SOURCES := $(addprefix src/,chared.c common.c el.c emacs.c filecomplete.c hist.c history.c \
	keymacro.c map.c parse.c prompt.c read.c readline.c refresh.c search.c sig.c terminal.c \
	tokenizer.c tty.c unvis.c vi.c vis.c chartype.c eln.c wcsdup.c) \
	$(addprefix local/,fcns.c help.c historyn.c tokenizern.c)
OBJS    := $(patsubst %.c,$(OBJ)/%.o,$(SOURCES))
DYLIB   := $(OBJ)/libedit.3.dylib

FLAGS := $(BASE_CFLAGS) -std=gnu99 -I$(SRCROOT) -I$(SRCROOT)/src -I$(SRCROOT)/local \
	-Wno-deprecated-non-prototype

all: $(DYLIB)

$(OBJ)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $(dir $@)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libedit.3.dylib \
		-compatibility_version 2 -current_version 3.0 \
		-Wl,-unexported_symbols_list,$(SRCROOT)/unexports $(OBJS) -lncurses -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	set -e; for d in $(SDK) $(ROOT); do \
		for l in libedit.2.dylib libedit.3.0.dylib libedit.dylib libreadline.dylib; do \
			ln -sf libedit.3.dylib $$d/usr/lib/$$l; \
		done; \
	done
	$(call install_xcode_man,$(SRCROOT)/libedit.xcodeproj)

installhdrs: fetch
	$(call copy_headers,$(SRCROOT)/src/histedit.h,usr/include)
	$(call copy_headers,$(SRCROOT)/src/editline/readline.h,usr/include/editline)
	install -d $(SDK)/usr/include/readline
	ln -sf ../editline/readline.h $(SDK)/usr/include/readline/readline.h
	ln -sf ../editline/readline.h $(SDK)/usr/include/readline/history.h

clean:
	rm -rf $(OBJ)

.PHONY: all install installhdrs clean
