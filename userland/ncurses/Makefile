# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := ncurses
TAG     := ncurses-79
include ../common.mk

S       := $(SRCROOT)/ncurses
GEN     := $(OBJ)/gen
HOSTCC  ?= clang-21
VERSION := 5.4

INCLUDES := -I$(GEN) -I$(S)/include -I$(S)/ncurses -I$(S)/progs -I$(S)/panel -I$(S)/form -I$(S)/menu
FLAGS := $(BASE_CFLAGS) -std=gnu99 -DHAVE_CONFIG_H -D_XOPEN_SOURCE=600 -DSIGWINCH=28 -DNDEBUG \
	-D_XOPEN_SOURCE_EXTENDED -DNCURSES_OPAQUE=0 -DNCURSES_WANT_BASEABI -D_NCURSES_LIBBUILD \
	$(INCLUDES) -Wno-deprecated-non-prototype

LIBS := ncurses panel form menu
srcs  = $(subst GEN/,$(GEN)/,$(shell cat lib$(1).list))
objs  = $(patsubst %.c,$(OBJ)/$(1)/%.o,$(subst GEN/,gen/,$(shell cat lib$(1).list)))

all: $(foreach l,$(LIBS),$(OBJ)/lib$(l).$(VERSION).dylib)

HOST_FLAGS := -DUSE_BUILD_CC -I$(GEN) -I$(S)/include -I$(S)/ncurses \
	'-D__OSX_AVAILABLE(v)=' '-D__IOS_AVAILABLE(v)=' '-D__WATCHOS_AVAILABLE(v)=' \
	'-D__TVOS_AVAILABLE(v)='
XCODE_ENV := PROJECT_DIR=$(SRCROOT) SRCROOT=$(SRCROOT) BUILT_PRODUCTS_DIR=$(GEN) \
	PLATFORM_NAME=macosx SDKROOT=$(SDK)
$(GEN)/.done: $(SRCROOT)/.fetched
	rm -rf $(GEN)
	mkdir -p $(GEN)
	env $(XCODE_ENV) sh $(SRCROOT)/xcodescripts/bootstrap_sources.sh
	env $(XCODE_ENV) sh $(SRCROOT)/xcodescripts/bootstrap_headers.sh
	$(HOSTCC) $(HOST_FLAGS) -o $(GEN)/make_keys $(S)/ncurses/tinfo/make_keys.c
	$(HOSTCC) $(HOST_FLAGS) -o $(GEN)/make_hash $(S)/ncurses/tinfo/make_hash.c
	env $(XCODE_ENV) UNIFDEF=unifdef sh $(SRCROOT)/xcodescripts/derived_headers.sh
	env $(XCODE_ENV) sh $(SRCROOT)/xcodescripts/derived_sources.sh
	touch $@
GEN_SRCS := $(sort $(subst GEN/,$(GEN)/,$(filter GEN/%,$(foreach l,$(LIBS),$(shell cat lib$(l).list)))))
$(GEN_SRCS): $(GEN)/.done ;

define lib_rules
$(OBJ)/$(1)/gen/%.o: $(GEN)/%.c $(GEN)/.done
	@mkdir -p $$(dir $$@)
	$(CC) $(FLAGS) -c $$< -o $$@
$(OBJ)/$(1)/%.o: $(SRCROOT)/%.c $(GEN)/.done
	@mkdir -p $$(dir $$@)
	$(CC) $(FLAGS) -c $$< -o $$@
$(OBJ)/lib$(1).$(VERSION).dylib: $$(call objs,$(1)) $(if $(filter-out ncurses,$(1)),$(OBJ)/libncurses.$(VERSION).dylib)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/lib$(1).$(VERSION).dylib \
		-compatibility_version $(VERSION) -current_version $(VERSION) $$^ -o $$@
endef
$(foreach l,$(LIBS),$(eval $(call lib_rules,$(l))))

PROGS := clear infocmp tic toe tput tset
clear_SRCS   := clear.c
infocmp_SRCS := infocmp.c dump_entry.c
tic_SRCS     := tic.c transform.c tparm_type.c dump_entry.c
toe_SRCS     := toe.c
tput_SRCS    := transform.c tput.c tparm_type.c
tset_SRCS    := transform.c tset.c
PROG_FLAGS := $(BASE_CFLAGS) -std=gnu99 $(INCLUDES) -Wno-deprecated-non-prototype
$(OBJ)/progs/%.o: $(S)/progs/%.c $(GEN)/.done
	@mkdir -p $(dir $@)
	$(CC) $(PROG_FLAGS) -c $< -o $@
define prog_rules
$(OBJ)/bin/$(1): $$(patsubst %.c,$(OBJ)/progs/%.o,$$($(1)_SRCS)) $(OBJ)/libncurses.$(VERSION).dylib
	@mkdir -p $$(dir $$@)
	$(CC) $(BASE_LDFLAGS) $$^ -o $$@
endef
$(foreach p,$(PROGS),$(eval $(call prog_rules,$(p))))

all: $(foreach p,$(PROGS),$(OBJ)/bin/$(p))

HOSTTIC ?= tic
$(OBJ)/terminfo/.done: $(SRCROOT)/.fetched
	rm -rf $(OBJ)/terminfo
	mkdir -p $(OBJ)/terminfo
	$(HOSTTIC) -x -o $(OBJ)/terminfo $(S)/misc/terminfo.src 2> $(OBJ)/tic.log
	cd $(OBJ)/terminfo && for d in ?; do mv "$$d" "$$(printf %02x "'$$d")"; done
	cd $(OBJ)/terminfo && find . -type l | while read l; do \
		t=$$(readlink "$$l"); \
		case "$$t" in ../?/*) \
			d=$${t#../}; c=$${d%%/*}; \
			ln -sfn "../$$(printf %02x "'$$c")/$${d#*/}" "$$l";; \
		esac; \
	done
	touch $@

all: $(OBJ)/terminfo/.done

install: all
	set -e; for d in $(SDK) $(ROOT); do \
		install -d $$d/usr/lib; \
		for l in $(LIBS); do \
			install -m 755 $(OBJ)/lib$$l.$(VERSION).dylib $$d/usr/lib/; \
			ln -sf lib$$l.$(VERSION).dylib $$d/usr/lib/lib$$l.dylib; \
		done; \
		for l in curses termcap ncurses.5; do ln -sf libncurses.$(VERSION).dylib $$d/usr/lib/lib$$l.dylib; done; \
	done
	$(call copy_headers,$(S)/include/tic.h $(S)/menu/eti.h $(S)/panel/panel.h \
		$(S)/include/ncurses_dll.h $(S)/include/unctrl.h $(S)/include/nc_tparm.h $(GEN)/term.h \
		$(S)/form/form.h $(S)/include/termcap.h $(GEN)/curses.h $(S)/menu/menu.h \
		$(S)/include/term_entry.h,usr/include)
	ln -sf curses.h $(SDK)/usr/include/ncurses.h
	install -d $(ROOT)/usr/bin
	install -m 755 $(addprefix $(OBJ)/bin/,$(PROGS)) $(ROOT)/usr/bin/
	ln -sf tset $(ROOT)/usr/bin/reset
	ln -sf tic $(ROOT)/usr/bin/captoinfo
	ln -sf tic $(ROOT)/usr/bin/infotocap
	install -d $(ROOT)/usr/share
	rm -rf $(ROOT)/usr/share/terminfo
	cp -a $(OBJ)/terminfo $(ROOT)/usr/share/terminfo
	rm -f $(ROOT)/usr/share/terminfo/.done
	$(call install_xcode_man,$(SRCROOT)/ncurses.xcodeproj)

clean:
	rm -rf $(OBJ)

.PHONY: all install clean
