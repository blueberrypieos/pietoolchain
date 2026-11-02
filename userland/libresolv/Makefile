# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The Blueberry Pie OS Authors

PROJECT := libresolv
TAG     := libresolv-96
include ../common.mk

SRCS := base64.c dns_async.c dns_util.c dns.c dst_api.c hmac_link.c support.c \
	res_state.c mtctxres.c ns_date.c ns_name.c res_mkupdate.c ns_netint.c ns_parse.c \
	ns_print.c ns_samedomain.c ns_sign.c ns_ttl.c ns_verify.c res_comp.c res_data.c \
	res_update.c res_debug.c res_findzonecut.c res_init.c res_mkquery.c res_query.c \
	res_send.c res_sendsigned.c
OBJS := $(addprefix $(OBJ)/,$(SRCS:.c=.o))
DYLIB := $(OBJ)/libresolv.9.dylib
FLAGS := $(BASE_CFLAGS) -std=gnu99 -Wall -Wno-deprecated-declarations \
	-I$(SRCROOT) -isystem $(SDK)/usr/local/include

all: $(DYLIB)

installhdrs: $(SRCROOT)/.fetched
	$(call copy_headers,$(addprefix $(SRCROOT)/,dns.h dns_util.h nameser.h resolv.h),usr/include)
	$(call copy_headers,$(SRCROOT)/dns_private.h,usr/local/include)
	install -d $(SDK)/usr/include/arpa
	ln -sf ../nameser.h $(SDK)/usr/include/arpa/nameser.h

$(OBJ)/%.o: $(SRCROOT)/%.c | installhdrs
	@mkdir -p $(OBJ)
	$(CC) $(FLAGS) -c $< -o $@

$(DYLIB): $(OBJS)
	$(CC) $(BASE_LDFLAGS) -dynamiclib -install_name /usr/lib/libresolv.9.dylib \
		-compatibility_version 1 -current_version 1 $^ -o $@

install: $(DYLIB) installhdrs
	$(call install_both,$(DYLIB),usr/lib)
	ln -sf libresolv.9.dylib $(SDK)/usr/lib/libresolv.dylib
	ln -sf libresolv.9.dylib $(ROOT)/usr/lib/libresolv.dylib
	$(call install_xcode_man,$(SRCROOT)/libresolv.xcodeproj)

clean:
	rm -rf $(OBJ)

.PHONY: all installhdrs install clean
