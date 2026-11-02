/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <Block.h>
#include <errno.h>
#include <os/lock.h>
#include <servers/bootstrap.h>
#include <stdlib.h>
#include <string.h>

const char *const _xpc_error_key_description = "XPCErrorDescription";

#define ERROR_OBJECT(var, text) \
	static struct xpc_obj var##_desc = { &_xpc_type_string, -1, \
	    { .buf = { (char *)(text), sizeof(text) - 1 } } }; \
	static char *var##_keys[] = { (char *)"XPCErrorDescription" }; \
	static struct xpc_obj *var##_vals[] = { &var##_desc }; \
	const struct _xpc_dictionary_s var = { { &_xpc_type_error, -1, \
	    { .dict = { var##_keys, var##_vals, 1, 1 } } } }

ERROR_OBJECT(_xpc_error_connection_interrupted, "Connection interrupted");
ERROR_OBJECT(_xpc_error_connection_invalid, "Connection invalid");
ERROR_OBJECT(_xpc_error_termination_imminent, "Termination imminent");

struct conn {
	os_unfair_lock lock;
	char *name;
	uint64_t flags;
	dispatch_queue_t queue;
	xpc_handler_t handler;
	void *context;
	xpc_finalizer_t finalizer;
	bool active;
	bool invalid;
};

static void conn_dispose(struct xpc_obj *o);

const struct _xpc_type_s _xpc_type_connection = { "connection", conn_dispose };
const struct _xpc_type_s _xpc_type_endpoint = { "endpoint", NULL };

static struct conn *
CONN(xpc_connection_t c)
{
	struct xpc_obj *o = XO(c);
	return (o != NULL && o->type == XPC_TYPE_CONNECTION) ? o->ext : NULL;
}

static void
conn_dispose(struct xpc_obj *o)
{
	struct conn *c = o->ext;
	if (c->finalizer != NULL) {
		c->finalizer(c->context);
	}
	if (c->handler != NULL) {
		Block_release(c->handler);
	}
	if (c->queue != NULL) {
		dispatch_release(c->queue);
	}
	free(c->name);
	free(c);
}

static dispatch_queue_t
conn_queue(struct conn *c)
{
	return c->queue ? c->queue : dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

static xpc_connection_t
conn_create(const char *name, dispatch_queue_t targetq, uint64_t flags)
{
	struct conn *c = calloc(1, sizeof(*c));
	if (c == NULL) {
		return NULL;
	}
	c->lock = OS_UNFAIR_LOCK_INIT;
	c->name = name ? strdup(name) : NULL;
	c->flags = flags;
	if (targetq != NULL) {
		dispatch_retain(targetq);
		c->queue = targetq;
	}
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_CONNECTION);
	o->ext = c;
	return (xpc_connection_t)o;
}

xpc_connection_t
xpc_connection_create(const char *name, dispatch_queue_t targetq)
{
	return conn_create(name, targetq, 0);
}

xpc_connection_t
xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags)
{
	return conn_create(name, targetq, flags);
}

xpc_connection_t
xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint)
{
	(void)endpoint;
	return conn_create(NULL, NULL, 0);
}

void
xpc_connection_set_target_queue(xpc_connection_t connection, dispatch_queue_t targetq)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	if (targetq != NULL) {
		dispatch_retain(targetq);
	}
	os_unfair_lock_lock(&c->lock);
	dispatch_queue_t old = c->queue;
	c->queue = targetq;
	os_unfair_lock_unlock(&c->lock);
	if (old != NULL) {
		dispatch_release(old);
	}
}

void
xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	xpc_handler_t h = handler ? Block_copy(handler) : NULL;
	os_unfair_lock_lock(&c->lock);
	xpc_handler_t old = c->handler;
	c->handler = h;
	os_unfair_lock_unlock(&c->lock);
	if (old != NULL) {
		Block_release(old);
	}
}

void
xpc_connection_activate(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c != NULL) {
		c->active = true;
	}
}

void xpc_connection_resume(xpc_connection_t connection) { xpc_connection_activate(connection); }
void xpc_connection_suspend(xpc_connection_t connection) { (void)connection; }

static void
conn_invalidate(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	os_unfair_lock_lock(&c->lock);
	bool deliver = !c->invalid;
	c->invalid = true;
	xpc_handler_t h = (deliver && c->handler) ? Block_copy(c->handler) : NULL;
	dispatch_queue_t q = conn_queue(c);
	os_unfair_lock_unlock(&c->lock);
	if (h == NULL) {
		return;
	}
	xpc_retain(connection);
	dispatch_async(q, ^{
		h(XPC_ERROR_CONNECTION_INVALID);
		Block_release(h);
		xpc_release(connection);
	});
}

void
xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message)
{
	(void)message;
	conn_invalidate(connection);
}

void
xpc_connection_send_barrier(xpc_connection_t connection, dispatch_block_t barrier)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	dispatch_async(conn_queue(c), barrier);
}

void
xpc_connection_send_message_with_reply(xpc_connection_t connection, xpc_object_t message,
    dispatch_queue_t replyq, xpc_handler_t handler)
{
	struct conn *c = CONN(connection);
	conn_invalidate(connection);
	if (handler == NULL) {
		return;
	}
	dispatch_queue_t q = replyq ? replyq : (c ? conn_queue(c) :
	    dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
	xpc_handler_t h = Block_copy(handler);
	(void)message;
	dispatch_async(q, ^{
		h(XPC_ERROR_CONNECTION_INVALID);
		Block_release(h);
	});
}

xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t connection, xpc_object_t message)
{
	(void)message;
	conn_invalidate(connection);
	return XPC_ERROR_CONNECTION_INVALID;
}

void
xpc_connection_cancel(xpc_connection_t connection)
{
	conn_invalidate(connection);
}

const char *
xpc_connection_get_name(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	return c ? c->name : NULL;
}

uid_t xpc_connection_get_euid(xpc_connection_t connection) { (void)connection; return (uid_t)-1; }
gid_t xpc_connection_get_egid(xpc_connection_t connection) { (void)connection; return (gid_t)-1; }
pid_t xpc_connection_get_pid(xpc_connection_t connection) { (void)connection; return 0; }
au_asid_t xpc_connection_get_asid(xpc_connection_t connection) { (void)connection; return 0; }

void
xpc_connection_set_context(xpc_connection_t connection, void *context)
{
	struct conn *c = CONN(connection);
	if (c != NULL) {
		c->context = context;
	}
}

void *
xpc_connection_get_context(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	return c ? c->context : NULL;
}

void
xpc_connection_set_finalizer_f(xpc_connection_t connection, xpc_finalizer_t finalizer)
{
	struct conn *c = CONN(connection);
	if (c != NULL) {
		c->finalizer = finalizer;
	}
}

xpc_connection_t
xpc_dictionary_get_remote_connection(xpc_object_t xdict)
{
	(void)xdict;
	return NULL;
}

xpc_object_t
xpc_dictionary_create_reply(xpc_object_t original)
{
	(void)original;
	return NULL;
}

struct pipe {
	mach_port_t port;
};

static void pipe_dispose(struct xpc_obj *o);
static const struct _xpc_type_s _xpc_type_pipe = { "pipe", pipe_dispose };

static void
pipe_dispose(struct xpc_obj *o)
{
	struct pipe *p = o->ext;
	if (p->port != MACH_PORT_NULL) {
		mach_port_deallocate(mach_task_self(), p->port);
	}
	free(p);
}

xpc_pipe_t
xpc_pipe_create_from_port(mach_port_t port, uint64_t flags)
{
	(void)flags;
	struct pipe *p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return NULL;
	}
	p->port = port;
	struct xpc_obj *o = _xpc_alloc(&_xpc_type_pipe);
	o->ext = p;
	return (xpc_pipe_t)o;
}

xpc_pipe_t
xpc_pipe_create(const char *name, uint64_t flags)
{
	mach_port_t port = MACH_PORT_NULL;
	if (bootstrap_look_up(bootstrap_port, name, &port) != KERN_SUCCESS) {
		return NULL;
	}
	return xpc_pipe_create_from_port(port, flags);
}

int
xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t message, xpc_object_t *reply,
    uint64_t flags)
{
	(void)pipe; (void)message; (void)flags;
	if (reply != NULL) {
		*reply = NULL;
	}
	return ENOTSUP;
}

int
xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t message, xpc_object_t *reply)
{
	return xpc_pipe_routine_with_flags(pipe, message, reply, 0);
}

int
xpc_pipe_simpleroutine(xpc_pipe_t pipe, xpc_object_t message)
{
	(void)pipe; (void)message;
	return ENOTSUP;
}

void
xpc_pipe_invalidate(xpc_pipe_t pipe)
{
	(void)pipe;
}
