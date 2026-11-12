/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef XPC_INTERNAL_H
#define XPC_INTERNAL_H

#include <xpc/xpc.h>
#include <xpc/private.h>
#include <mach/mach.h>
#include <stdatomic.h>

#define XPC_HIDDEN __attribute__((visibility("hidden")))

struct xpc_obj;

struct _xpc_type_s {
	const char *name;
	void (*dispose)(struct xpc_obj *o);
};

struct xpc_msginfo {
	audit_token_t token;
	bool has_token;
	bool is_reply;
	mach_port_t reply;
	xpc_connection_t conn;
};

struct xpc_obj {
	xpc_type_t type;
	_Atomic int32_t refcnt;
	union {
		bool b;
		int64_t i;
		uint64_t u;
		double d;
		mach_port_t port;
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
			struct xpc_msginfo *msg;
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
XPC_HIDDEN xpc_object_t _xpc_buf_create(xpc_type_t type, const void *bytes, size_t length);
XPC_HIDDEN xpc_object_t _xpc_port_create(xpc_type_t type, mach_port_t port);
XPC_HIDDEN struct xpc_msginfo *_xpc_dictionary_msginfo(xpc_object_t xdict, bool create);
XPC_HIDDEN void _xpc_dictionary_set_connection(xpc_object_t xdict, xpc_connection_t conn);
XPC_HIDDEN mach_port_t _xpc_dictionary_take_reply(xpc_object_t xdict);
XPC_HIDDEN void _xpc_dictionary_append(xpc_object_t xdict, char *key, xpc_object_t value);
XPC_HIDDEN bool _xpc_dictionary_keys_unique(xpc_object_t xdict);
XPC_HIDDEN void _xpc_port_release(mach_port_t port, mach_msg_type_name_t type);

#define XO(x) ((struct xpc_obj *)(x))

#define XPC_WIRE_MESSAGE 0x70786301
#define XPC_WIRE_REQUEST 0x70786302
#define XPC_WIRE_REPLY   0x70786303
#define XPC_WIRE_WAKE    0x70786304
#define XPC_WIRE_CONNECT 0x70786305
#define XPC_WIRE_PORTS_MAX 16383u

struct xpc_wire_out {
	mach_port_t dest;
	mach_msg_type_name_t dest_type;
	mach_port_t reply;
	mach_msg_type_name_t reply_type;
	mach_port_t port0;
	mach_msg_type_name_t port0_type;
	mach_port_t port1;
	mach_msg_type_name_t port1_type;
	mach_msg_id_t id;
	bool block;
};

struct xpc_wire_held {
	mach_msg_header_t *head;
	void *ool;
	mach_port_t *ports;
	size_t nports;
	bool refs;
	mach_port_t receive;
	unsigned fixed;
};

enum {
	XPC_WIRE_OK,
	XPC_WIRE_NOT_XPC,
	XPC_WIRE_BAD,
};

XPC_HIDDEN kern_return_t _xpc_wire_send(xpc_object_t message, const struct xpc_wire_out *out);
XPC_HIDDEN bool _xpc_wire_kept(kern_return_t kr);
XPC_HIDDEN void _xpc_wire_destroy(mach_msg_header_t *head);
XPC_HIDDEN void _xpc_wire_destroy_body(mach_msg_header_t *head);
XPC_HIDDEN kern_return_t _xpc_wire_prepare(xpc_object_t message, const struct xpc_wire_out *out,
    struct xpc_wire_held *h);
XPC_HIDDEN kern_return_t _xpc_wire_try(struct xpc_wire_held *h);
XPC_HIDDEN kern_return_t _xpc_wire_hold(struct xpc_wire_held *h);
XPC_HIDDEN void _xpc_wire_address(struct xpc_wire_held *h, const struct xpc_wire_out *out);
XPC_HIDDEN void _xpc_wire_release(struct xpc_wire_held *h);
XPC_HIDDEN kern_return_t _xpc_wire_receive(mach_port_t port, mach_msg_option_t options,
    mach_msg_timeout_t timeout, mach_msg_header_t **message);
XPC_HIDDEN int _xpc_wire_decode(mach_msg_header_t *head, xpc_object_t *message,
    mach_port_t *port0, mach_port_t *port1);
XPC_HIDDEN const audit_token_t *_xpc_wire_audit_token(mach_msg_header_t *head);

XPC_HIDDEN extern _Atomic uint32_t _xpc_fork_generation;

#endif
