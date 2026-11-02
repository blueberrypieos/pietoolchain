/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef XPC_INTERNAL_H
#define XPC_INTERNAL_H

#include <xpc/xpc.h>
#include <xpc/private.h>
#include <stdatomic.h>

struct xpc_obj;

struct _xpc_type_s {
	const char *name;
	void (*dispose)(struct xpc_obj *o);
};

struct xpc_obj {
	xpc_type_t type;
	_Atomic int32_t refcnt;
	union {
		bool b;
		int64_t i;
		uint64_t u;
		double d;
		struct {
			char *ptr;
			size_t len;
		} buf;
		uuid_t uuid;
		struct {
			struct xpc_obj **v;
			size_t n, cap;
		} arr;
		struct {
			char **keys;
			struct xpc_obj **vals;
			size_t n, cap;
		} dict;
		void *ext;
	};
};

struct _xpc_bool_s {
	struct xpc_obj obj;
};

struct _xpc_dictionary_s {
	struct xpc_obj obj;
};

struct xpc_obj *_xpc_alloc(xpc_type_t type);

#define XO(x) ((struct xpc_obj *)(x))

#endif
