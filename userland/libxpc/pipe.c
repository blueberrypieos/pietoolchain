/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <bootstrap_priv.h>
#include <errno.h>
#include <mach/mig.h>
#include <mach/notify.h>
#include <servers/bootstrap.h>
#include <stdlib.h>
#include <string.h>

_Atomic uint32_t _xpc_fork_generation;

struct pipe {
	mach_port_t port;
	uint64_t flags;
	uint32_t generation;
	_Atomic bool invalid;
};

static void pipe_dispose(struct xpc_obj *o);
static const struct _xpc_type_s _xpc_type_pipe = { "pipe", pipe_dispose };

static struct pipe *
PIPE(xpc_pipe_t pipe)
{
	struct xpc_obj *o = XO(pipe);
	return (o != NULL && o->type == &_xpc_type_pipe) ? o->ext : NULL;
}

static void
pipe_dispose(struct xpc_obj *o)
{
	struct pipe *p = o->ext;
	if (p->generation == atomic_load(&_xpc_fork_generation)) {
		_xpc_port_release(p->port, MACH_MSG_TYPE_PORT_SEND);
	}
	free(p);
}

xpc_pipe_t
xpc_pipe_create_from_port(mach_port_t port, uint64_t flags)
{
	struct pipe *p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return NULL;
	}
	p->port = port;
	p->flags = flags;
	p->generation = atomic_load(&_xpc_fork_generation);
	struct xpc_obj *o = _xpc_alloc(&_xpc_type_pipe);
	o->ext = p;
	return (xpc_pipe_t)o;
}

xpc_pipe_t
xpc_pipe_create(const char *name, uint64_t flags)
{
	mach_port_t port = MACH_PORT_NULL;
	if (name == NULL || bootstrap_look_up2(bootstrap_port, name, &port, 0,
	    (flags & XPC_PIPE_PRIVILEGED) ? BOOTSTRAP_PRIVILEGED_SERVER : 0) != KERN_SUCCESS) {
		return NULL;
	}
	return xpc_pipe_create_from_port(port, flags);
}

static int
pipe_send(xpc_pipe_t pipe, xpc_object_t message, mach_port_t reply, mach_msg_id_t id, bool *stale)
{
	struct pipe *p = PIPE(pipe);
	*stale = false;
	if (p == NULL || message == NULL || XO(message)->type != XPC_TYPE_DICTIONARY) {
		return EINVAL;
	}
	if (atomic_load(&p->invalid) || p->generation != atomic_load(&_xpc_fork_generation)) {
		return EPIPE;
	}
	struct xpc_wire_out out = {
		.dest = p->port,
		.dest_type = MACH_MSG_TYPE_COPY_SEND,
		.reply = reply,
		.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE,
		.id = id,
		.block = true,
	};
	kern_return_t kr = _xpc_wire_send(message, &out);
	if (kr == KERN_SUCCESS) {
		return 0;
	}
	*stale = !_xpc_wire_kept(kr);
	if (kr == KERN_INVALID_ARGUMENT) {
		return EINVAL;
	}
	if (kr == MACH_SEND_INVALID_DEST) {
		atomic_store(&p->invalid, true);
		return EPIPE;
	}
	return EIO;
}

int
xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t message, xpc_object_t *reply,
    uint64_t flags)
{
	(void)flags;
	if (reply != NULL) {
		*reply = NULL;
	}
	mach_port_t port = mig_get_reply_port();
	bool stale;
	int rc = pipe_send(pipe, message, port, XPC_WIRE_REQUEST, &stale);
	if (rc != 0) {
		if (stale) {
			mig_dealloc_reply_port(port);
		}
		return rc;
	}
	mach_msg_header_t *head;
	if (_xpc_wire_receive(port, 0, 0, &head) != KERN_SUCCESS) {
		mig_dealloc_reply_port(port);
		return EIO;
	}
	xpc_object_t r;
	mach_port_t p0, p1;
	int d = _xpc_wire_decode(head, &r, &p0, &p1);
	mach_msg_id_t id = head->msgh_id;
	if (d == XPC_WIRE_NOT_XPC) {
		mach_msg_destroy(head);
	}
	free(head);
	if (d != XPC_WIRE_OK) {
		return id == MACH_NOTIFY_SEND_ONCE ? EPIPE : EIO;
	}
	_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
	_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
	if (id != XPC_WIRE_REPLY) {
		xpc_release(r);
		return EIO;
	}
	if (reply != NULL) {
		*reply = r;
	} else {
		xpc_release(r);
	}
	return 0;
}

int
xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t message, xpc_object_t *reply)
{
	return xpc_pipe_routine_with_flags(pipe, message, reply, 0);
}

int
xpc_pipe_simpleroutine(xpc_pipe_t pipe, xpc_object_t message)
{
	bool stale;
	return pipe_send(pipe, message, MACH_PORT_NULL, XPC_WIRE_MESSAGE, &stale);
}

void
xpc_pipe_invalidate(xpc_pipe_t pipe)
{
	struct pipe *p = PIPE(pipe);
	if (p != NULL) {
		atomic_store(&p->invalid, true);
	}
}

static int
pipe_receive(mach_port_t port, xpc_object_t *message, mach_port_t *recvp,
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *), mach_msg_size_t msgsize,
    mach_msg_option_t options)
{
	*message = NULL;
	mach_msg_header_t *head;
	kern_return_t kr = _xpc_wire_receive(port, options, 0, &head);
	if (kr == MACH_RCV_TIMED_OUT) {
		return EAGAIN;
	}
	if (kr != KERN_SUCCESS) {
		return kr == MACH_RCV_INVALID_NAME || kr == MACH_RCV_TOO_LARGE ? EINVAL : EIO;
	}
	if (recvp != NULL) {
		*recvp = head->msgh_local_port;
	}
	mach_port_t p0, p1;
	int d = _xpc_wire_decode(head, message, &p0, &p1);
	if (d == XPC_WIRE_OK) {
		_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
		bool connect = head->msgh_id == XPC_WIRE_CONNECT;
		free(head);
		if (connect) {
			xpc_release(*message);
			*message = NULL;
			return EINVAL;
		}
		return 0;
	}
	if (d == XPC_WIRE_BAD) {
		free(head);
		return EINVAL;
	}
	if (demux == NULL) {
		mach_msg_destroy(head);
		free(head);
		return EINVAL;
	}
	if (msgsize < sizeof(mig_reply_error_t)) {
		msgsize = sizeof(mig_reply_error_t);
	}
	mig_reply_error_t *rep = calloc(1, msgsize + MAX_TRAILER_SIZE);
	if (rep == NULL) {
		mach_msg_destroy(head);
		free(head);
		return ENOMEM;
	}
	if (!demux(head, &rep->Head)) {
		rep->RetCode = MIG_BAD_ID;
	}
	if (!(rep->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) && rep->RetCode != KERN_SUCCESS) {
		if (rep->RetCode == MIG_NO_REPLY) {
			rep->Head.msgh_remote_port = MACH_PORT_NULL;
		} else {
			head->msgh_remote_port = MACH_PORT_NULL;
			mach_msg_destroy(head);
		}
	}
	if (MACH_PORT_VALID(rep->Head.msgh_remote_port)) {
		mach_msg_option_t o = MACH_SEND_MSG;
		if (MACH_MSGH_BITS_REMOTE(rep->Head.msgh_bits) != MACH_MSG_TYPE_MOVE_SEND_ONCE) {
			o |= MACH_SEND_TIMEOUT;
		}
		kr = mach_msg(&rep->Head, o, rep->Head.msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
		if ((kr & ~MACH_MSG_MASK) == MACH_SEND_TIMED_OUT ||
		    (kr & ~MACH_MSG_MASK) == MACH_SEND_NO_BUFFER || _xpc_wire_kept(kr)) {
			_xpc_wire_destroy(&rep->Head);
		} else if (kr == MACH_SEND_INVALID_REPLY || kr == MACH_SEND_INVALID_OPTIONS ||
		    kr == MACH_SEND_MSG_FILTERED) {
			_xpc_wire_destroy_body(&rep->Head);
		}
	} else if (rep->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		mach_msg_destroy(&rep->Head);
	}
	free(rep);
	free(head);
	return 0;
}

int
xpc_pipe_receive(mach_port_t port, xpc_object_t *message)
{
	for (;;) {
		int rc = pipe_receive(port, message, NULL, NULL, 0, 0);
		if (rc != EINVAL || !MACH_PORT_VALID(port)) {
			return rc;
		}
		mach_port_type_t type;
		if (mach_port_type(mach_task_self(), port, &type) != KERN_SUCCESS ||
		    !(type & (MACH_PORT_TYPE_RECEIVE | MACH_PORT_TYPE_PORT_SET))) {
			return rc;
		}
	}
}

int
xpc_pipe_try_receive(mach_port_t port, xpc_object_t *message, mach_port_t *recvp,
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *), mach_msg_size_t msgsize,
    uint64_t flags)
{
	(void)flags;
	return pipe_receive(port, message, recvp, demux, msgsize, MACH_RCV_TIMEOUT);
}

int
xpc_pipe_routine_reply(xpc_object_t reply)
{
	if (reply == NULL || XO(reply)->type != XPC_TYPE_DICTIONARY) {
		return EINVAL;
	}
	mach_port_t port = _xpc_dictionary_take_reply(reply);
	if (!MACH_PORT_VALID(port)) {
		return EINVAL;
	}
	struct xpc_wire_out out = {
		.dest = port,
		.dest_type = MACH_MSG_TYPE_MOVE_SEND_ONCE,
		.id = XPC_WIRE_REPLY,
	};
	kern_return_t kr = _xpc_wire_send(reply, &out);
	if (kr != KERN_SUCCESS) {
		if (_xpc_wire_kept(kr)) {
			mach_port_deallocate(mach_task_self(), port);
		}
		return kr == KERN_INVALID_ARGUMENT ? EINVAL : EPIPE;
	}
	return 0;
}
