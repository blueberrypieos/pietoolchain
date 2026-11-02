# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

YACC ?= bison -y
LEX  ?= flex
TOOLS_BASE_CFLAGS = $(BASE_CFLAGS) -isystem $(SDK)/usr/local/include \
	-Wno-deprecated-non-prototype $(TOOLS_CFLAGS)

.SUFFIXES:

define tool_rules
$(1)_OBJS := $$(patsubst %,$(OBJ)/$(1)/%.o,$$(basename $$($(1)_SRCS)))
$(OBJ)/$(1)/%.o: $(SRCROOT)/%.c $(SRCROOT)/.fetched
	@mkdir -p $$(dir $$@)
	$(CC) $$(TOOLS_BASE_CFLAGS) $$($(1)_FLAGS) -c $$< -o $$@
$(OBJ)/$(1)/%.o: $(SRCROOT)/%.cc $(SRCROOT)/.fetched
	@mkdir -p $$(dir $$@)
	$(CXX) $$(TOOLS_BASE_CFLAGS) $$($(1)_FLAGS) -c $$< -o $$@
$(OBJ)/$(1)/%.o: $(SRCROOT)/%.cpp $(SRCROOT)/.fetched
	@mkdir -p $$(dir $$@)
	$(CXX) $$(TOOLS_BASE_CFLAGS) $$($(1)_FLAGS) -c $$< -o $$@
$(OBJ)/$(1)/%.c: $(SRCROOT)/%.y $(SRCROOT)/.fetched
	@mkdir -p $$(dir $$@)
	$(YACC) -d -o $$@ $$<
$(OBJ)/$(1)/%.c: $(SRCROOT)/%.l $(SRCROOT)/.fetched
	@mkdir -p $$(dir $$@)
	$(LEX) -o $$@ $$<
$(OBJ)/$(1)/%.o: $(OBJ)/$(1)/%.c
	$(CC) $$(TOOLS_BASE_CFLAGS) $$($(1)_FLAGS) -I$$(dir $$(patsubst $(OBJ)/$(1)/%,$(SRCROOT)/%,$$<)) \
		-I$$(dir $$<) -c $$< -o $$@
$(OBJ)/bin/$(1): $$($(1)_OBJS)
	@mkdir -p $$(dir $$@)
	$$(if $$(filter %.cc %.cpp,$$($(1)_SRCS)),$(CXX),$(CC)) $(BASE_LDFLAGS) $$^ $$($(1)_LIBS) \
		$(TOOLS_LDFLAGS) -o $$@
endef
$(foreach t,$(TOOLS),$(eval $(call tool_rules,$(t))))

TOOL_BINS := $(foreach t,$(TOOLS),$(OBJ)/bin/$(t))

install-tools: $(TOOL_BINS)
	set -e; $(foreach t,$(TOOLS),install -d $(ROOT)/$($(t)_DIR); \
		install -m $(or $($(t)_MODE),755) $(OBJ)/bin/$(t) $(ROOT)/$($(t)_DIR)/;)
	set -e; $(foreach l,$(LINKS),install -d $(dir $(ROOT)/$(lastword $(subst :, ,$(l)))); \
		ln -f $(ROOT)/$(firstword $(subst :, ,$(l))) $(ROOT)/$(lastword $(subst :, ,$(l)));)
	set -e; $(foreach m,$(foreach t,$(TOOLS),$($(t)_MANS)) $(MANS),install -d \
		$(ROOT)/usr/share/man/$(lastword $(subst :, ,$(m))); install -m 644 \
		$(SRCROOT)/$(firstword $(subst :, ,$(m))) $(ROOT)/usr/share/man/$(lastword $(subst :, ,$(m)))/;)

.PRECIOUS: $(OBJ)/%.c

.PHONY: install-tools
