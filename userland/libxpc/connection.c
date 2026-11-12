/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <Block.h>
#include <bootstrap_priv.h>
#include <errno.h>
#include <mach/notify.h>
#include <os/lock.h>
#include <os/log.h>
#include <servers/bootstrap.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

const char *const _xpc_error_key_description = "XPCErrorDescription";

#define ERROR_OBJECT(var, text) \
	static struct xpc_obj var##_desc = { &_xpc_type_string, -1, \
	    { .buf = { (char *)(text), sizeof(text) - 1 } } }; \
	static char *var##_keys[] = { (char *)"XPCErrorDescription" }; \
	static struct xpc_obj *var##_vals[] = { &var##_desc }; \
	const struct _xpc_dictionary_s var = { { &_xpc_type_error, -1, \
	    { .dict = { var##_keys, var##_vals, 1, 1, NULL } } } }

ERROR_OBJECT(_xpc_error_connection_interrupted, "Connection interrupted");
ERROR_OBJECT(_xpc_error_connection_invalid, "Connection invalid");
ERROR_OBJECT(_xpc_error_termination_imminent, "Termination imminent");

enum kind {
	KIND_NONE,
	KIND_CLIENT,
	KIND_LISTENER,
	KIND_PEER,
};

struct pending {
	struct pending *next;
	struct xpc_obj *conn;
	mach_port_t port;
	dispatch_source_t source;
	dispatch_queue_t replyq;
	xpc_handler_t handler;
	bool done;
	bool sync;
};

struct queued {
	struct queued *next;
	struct xpc_wire_held msg;
	mach_port_t service;
	mach_port_t once;
	dispatch_block_t barrier;
};

struct conn {
	os_unfair_lock lock;
	struct xpc_obj *obj;
	char *name;
	uint64_t flags;
	enum kind kind;
	dispatch_queue_t q;
	dispatch_queue_t rq;
	dispatch_queue_t target;
	xpc_handler_t handler;
	void *context;
	xpc_finalizer_t finalizer;
	int suspended;
	bool activated;
	bool cancelled;
	bool privileged;
	mach_port_t service;
	mach_port_t remote;
	mach_port_t local;
	dispatch_source_t source;
	dispatch_source_t recv_source;
	struct pending *pending;
	audit_token_t token;
	bool has_token;
	struct queued *sendq, **sendq_tail;
	mach_port_t sendq_dest;
	mach_port_t flush_dest;
	unsigned epoch;
	bool flushing;
	bool sp_armed;
	mach_port_t watch;
	struct conn *watch_next;
	bool watch_dead;
	bool discard;
	bool draining;
	bool connecting;
};

enum send_result {
	SEND_OK,
	SEND_INVALID,
	SEND_INTERRUPTED,
	SEND_BAD_MESSAGE,
};

static bool
refused(kern_return_t kr)
{
	return kr != KERN_SUCCESS && kr != MACH_SEND_INVALID_DEST && _xpc_wire_kept(kr);
}

static enum send_result
bad_message(struct conn *c, kern_return_t kr)
{
	os_log_error(OS_LOG_DEFAULT, "libxpc: connection %s: a message was not sent: 0x%x",
	    c->name != NULL ? c->name : "(peer)", kr);
	return SEND_BAD_MESSAGE;
}

static void
queued_free(struct queued *q)
{
	if (MACH_PORT_VALID(q->service)) {
		mach_port_deallocate(mach_task_self(), q->service);
	}
	if (MACH_PORT_VALID(q->once)) {
		mach_port_deallocate(mach_task_self(), q->once);
	}
	_xpc_wire_release(&q->msg);
	free(q);
}

static void send_end(struct xpc_obj *co);

static void
barrier_post(struct xpc_obj *co, struct queued *q, bool release)
{
	struct conn *c = co->ext;
	dispatch_async(c->q, ^{
		q->barrier();
		Block_release(q->barrier);
		free(q);
		if (release) {
			send_end(co);
		}
		xpc_release(co);
	});
}

static void
discard_queue(struct xpc_obj *co, struct queued *q, mach_port_t dest)
{
	while (q != NULL) {
		struct queued *next = q->next;
		if (q->barrier != NULL) {
			barrier_post(co, q, false);
		} else {
			queued_free(q);
		}
		q = next;
	}
	if (MACH_PORT_VALID(dest)) {
		mach_port_deallocate(mach_task_self(), dest);
	}
}

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
	while (c->suspended > 0) {
		c->suspended--;
		dispatch_resume(c->q);
	}
	dispatch_release(c->q);
	if (c->rq != NULL) {
		dispatch_release(c->rq);
	}
	if (c->target != NULL) {
		dispatch_release(c->target);
	}
	discard_queue(o, c->sendq, c->sendq_dest);
	if (MACH_PORT_VALID(c->flush_dest)) {
		mach_port_deallocate(mach_task_self(), c->flush_dest);
	}
	if (c->recv_source != NULL) {
		dispatch_release(c->recv_source);
	}
	free(c->name);
	free(c);
}

static struct xpc_obj *
conn_new(const char *name, enum kind kind, uint64_t flags, dispatch_queue_t target,
    dispatch_queue_t rq)
{
	struct conn *c = calloc(1, sizeof(*c));
	if (c == NULL) {
		return NULL;
	}
	c->lock = OS_UNFAIR_LOCK_INIT;
	c->sendq_tail = &c->sendq;
	c->name = name ? strdup(name) : NULL;
	c->flags = flags;
	c->kind = kind;
	c->privileged = (flags & XPC_CONNECTION_MACH_SERVICE_PRIVILEGED) != 0;
	c->q = dispatch_queue_create("org.blueberrypieos.xpc.connection", NULL);
	dispatch_suspend(c->q);
	c->suspended = 1;
	if (target != NULL) {
		dispatch_retain(target);
		c->target = target;
		dispatch_set_target_queue(c->q, target);
	}
	if (rq != NULL) {
		dispatch_retain(rq);
		c->rq = rq;
	} else if (kind != KIND_NONE) {
		c->rq = dispatch_queue_create("org.blueberrypieos.xpc.receive", NULL);
	}
	struct xpc_obj *o = _xpc_alloc(XPC_TYPE_CONNECTION);
	o->ext = c;
	c->obj = o;
	return o;
}

static void conn_invalidate(struct xpc_obj *co);

static void
dropped(xpc_object_t event)
{
	if (XO(event)->type == XPC_TYPE_CONNECTION) {
		conn_invalidate(XO(event));
	}
}

static void
deliver_rx(struct xpc_obj *co, xpc_object_t event, bool hold)
{
	struct conn *c = co->ext;
	bool error = XO(event)->type == XPC_TYPE_ERROR;
	os_unfair_lock_lock(&c->lock);
	if (c->cancelled) {
		os_unfair_lock_unlock(&c->lock);
		dropped(event);
		return;
	}
	dispatch_source_t held = hold ? c->recv_source : NULL;
	if (held != NULL) {
		dispatch_suspend(held);
	}
	xpc_retain(co);
	xpc_retain(event);
	dispatch_async(c->q, ^{
		os_unfair_lock_lock(&c->lock);
		bool drop = !error && c->discard;
		xpc_handler_t h = c->handler != NULL && !drop ? Block_copy(c->handler) : NULL;
		os_unfair_lock_unlock(&c->lock);
		if (h != NULL) {
			h(event);
			Block_release(h);
		}
		if (drop) {
			dropped(event);
		}
		xpc_release(event);
		if (held != NULL) {
			dispatch_resume(held);
		}
		xpc_release(co);
	});
	os_unfair_lock_unlock(&c->lock);
}

static void
deliver(struct xpc_obj *co, xpc_object_t event)
{
	deliver_rx(co, event, false);
}

static void
deliver_final_locked(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	xpc_retain(co);
	dispatch_async(c->q, ^{
		os_unfair_lock_lock(&c->lock);
		xpc_handler_t h = c->handler != NULL ? Block_copy(c->handler) : NULL;
		os_unfair_lock_unlock(&c->lock);
		if (h != NULL) {
			h(XPC_ERROR_CONNECTION_INVALID);
			Block_release(h);
		}
		xpc_release(co);
	});
}

static void note_token(struct conn *c, xpc_object_t message);

static bool
pending_unlink_locked(struct conn *c, struct pending *p)
{
	for (struct pending **pp = &c->pending; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == p) {
			*pp = p->next;
			return true;
		}
	}
	return false;
}

static void
pending_finish(struct pending *p, xpc_object_t result)
{
	struct conn *c = p->conn->ext;
	bool mine = false;
	os_unfair_lock_lock(&c->lock);
	if (!p->done) {
		p->done = true;
		mine = true;
		pending_unlink_locked(c, p);
	}
	os_unfair_lock_unlock(&c->lock);
	if (!mine) {
		return;
	}
	xpc_handler_t h = p->handler;
	xpc_retain(result);
	dispatch_async(p->replyq, ^{
		h(result);
		xpc_release(result);
	});
	dispatch_source_cancel(p->source);
}

static void
pending_receive(struct pending *p)
{
	mach_msg_header_t *head;
	kern_return_t kr = _xpc_wire_receive(p->port, MACH_RCV_TIMEOUT, 0, &head);
	if (kr == MACH_RCV_TOO_LARGE || kr == MACH_RCV_BODY_ERROR) {
		pending_finish(p, XPC_ERROR_CONNECTION_INTERRUPTED);
	}
	if (kr != KERN_SUCCESS) {
		return;
	}
	xpc_object_t reply = NULL;
	mach_port_t p0, p1;
	int r = _xpc_wire_decode(head, &reply, &p0, &p1);
	if (r == XPC_WIRE_OK) {
		_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
		if (head->msgh_id != XPC_WIRE_REPLY) {
			xpc_release(reply);
			reply = NULL;
		}
	} else if (r == XPC_WIRE_NOT_XPC) {
		mach_msg_destroy(head);
	}
	free(head);
	if (reply != NULL) {
		note_token(p->conn->ext, reply);
		_xpc_dictionary_set_connection(reply, (xpc_connection_t)p->conn);
		pending_finish(p, reply);
		xpc_release(reply);
	} else {
		pending_finish(p, XPC_ERROR_CONNECTION_INTERRUPTED);
	}
}

static void
fail_pending(struct pending *list, xpc_object_t error)
{
	while (list != NULL) {
		struct pending *next = list->next;
		xpc_handler_t h = list->handler;
		dispatch_async(list->replyq, ^{
			h(error);
		});
		dispatch_source_cancel(list->source);
		list = next;
	}
}

static void
wake_locked(struct pending *p)
{
	mach_msg_header_t m = {
		.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_MAKE_SEND_ONCE, 0, 0, 0),
		.msgh_size = sizeof(m),
		.msgh_remote_port = p->port,
		.msgh_id = XPC_WIRE_WAKE,
	};
	mach_msg(&m, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0,
	    MACH_PORT_NULL);
}

static bool
from_kernel(mach_msg_header_t *head)
{
	const audit_token_t *tok = _xpc_wire_audit_token(head);
	return !(head->msgh_bits & MACH_MSGH_BITS_COMPLEX) && tok != NULL && tok->val[5] == 0;
}

static mach_port_t
kernel_notification(mach_msg_header_t *head, mach_msg_id_t id)
{
	if (head->msgh_id != id ||
	    head->msgh_size < offsetof(mach_dead_name_notification_t, trailer) ||
	    !from_kernel(head)) {
		return MACH_PORT_NULL;
	}
	return ((mach_dead_name_notification_t *)head)->not_port;
}

#define NOTE_BUCKETS 256

struct note {
	struct note *next;
	mach_port_t name;
	unsigned requests;
	struct conn *watchers;
};

static os_unfair_lock notes_lock = OS_UNFAIR_LOCK_INIT;
static struct note *notes[NOTE_BUCKETS];
static mach_port_t notify_port;

static void notify_receive(void);

static kern_return_t
notify_start(void)
{
	if (MACH_PORT_VALID(notify_port)) {
		return KERN_SUCCESS;
	}
	mach_port_options_t opts = { .flags = 0 };
	mach_port_t port = MACH_PORT_NULL;
	kern_return_t kr = mach_port_construct(mach_task_self(), &opts, 0, &port);
	if (kr != KERN_SUCCESS) {
		return kr;
	}
	dispatch_queue_t q = dispatch_queue_create("org.blueberrypieos.xpc.notify", NULL);
	dispatch_source_t src = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, port, 0, q);
	dispatch_release(q);
	dispatch_source_set_event_handler(src, ^{
		notify_receive();
	});
	dispatch_resume(src);
	notify_port = port;
	return KERN_SUCCESS;
}

static struct note **
note_find(mach_port_t name)
{
	struct note **np = &notes[MACH_PORT_INDEX(name) % NOTE_BUCKETS];
	while (*np != NULL && (*np)->name != name) {
		np = &(*np)->next;
	}
	return np;
}

static void
note_put(struct note **np)
{
	struct note *n = *np;
	if (n->requests == 0 && n->watchers == NULL) {
		*np = n->next;
		free(n);
	}
}

static kern_return_t
note_request(struct note *n, bool possible, mach_port_t notify)
{
	mach_port_t previous = MACH_PORT_NULL;
	kern_return_t kr = mach_port_request_notification(mach_task_self(), n->name,
	    MACH_NOTIFY_SEND_POSSIBLE, possible ? 1 : 0, notify, MACH_MSG_TYPE_MAKE_SEND_ONCE,
	    &previous);
	if (kr == KERN_SUCCESS && MACH_PORT_VALID(notify)) {
		n->requests++;
	}
	if (MACH_PORT_VALID(previous)) {
		if (n->requests > 0) {
			n->requests--;
		}
		mach_port_deallocate(mach_task_self(), previous);
	}
	return kr;
}

static void
unwatch_locked(struct conn *c)
{
	if (!MACH_PORT_VALID(c->watch)) {
		return;
	}
	os_unfair_lock_lock(&notes_lock);
	struct note **np = note_find(c->watch);
	struct note *n = *np;
	for (struct conn **wp = &n->watchers; *wp != NULL; wp = &(*wp)->watch_next) {
		if (*wp == c) {
			*wp = c->watch_next;
			break;
		}
	}
	if (n->watchers == NULL && n->requests > 0) {
		note_request(n, false, MACH_PORT_NULL);
	}
	note_put(np);
	c->watch = MACH_PORT_NULL;
	c->watch_next = NULL;
	c->watch_dead = false;
	os_unfair_lock_unlock(&notes_lock);
}

static kern_return_t
watch_locked(struct conn *c, mach_port_t name, bool possible)
{
	if (c->watch != name) {
		unwatch_locked(c);
	}
	os_unfair_lock_lock(&notes_lock);
	kern_return_t kr = notify_start();
	if (kr == KERN_SUCCESS) {
		struct note **np = note_find(name);
		if (*np == NULL) {
			*np = calloc(1, sizeof(**np));
			if (*np == NULL) {
				abort();
			}
			(*np)->name = name;
		}
		struct note *n = *np;
		if (c->watch != name) {
			c->watch = name;
			c->watch_next = n->watchers;
			n->watchers = c;
		}
		if (!possible) {
			c->watch_dead = true;
		}
		if (possible || n->requests == 0) {
			kr = note_request(n, possible, notify_port);
		}
	}
	os_unfair_lock_unlock(&notes_lock);
	return kr;
}

static struct queued *
drop_locked(struct conn *c, mach_port_t *dest)
{
	struct queued *q = c->sendq;
	*dest = c->sendq_dest;
	c->sendq = NULL;
	c->sendq_tail = &c->sendq;
	c->sendq_dest = MACH_PORT_NULL;
	c->sp_armed = false;
	c->epoch++;
	if (c->flushing && !MACH_PORT_VALID(c->flush_dest)) {
		c->flush_dest = *dest;
		*dest = MACH_PORT_NULL;
	}
	return q;
}

static void
conn_cancel(struct xpc_obj *co, bool discard)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	if (discard) {
		c->discard = true;
	}
	if (c->cancelled) {
		os_unfair_lock_unlock(&c->lock);
		return;
	}
	c->cancelled = true;
	deliver_final_locked(co);
	dispatch_source_t src = c->source;
	c->source = NULL;
	struct pending *list = NULL, **tail = &list;
	for (struct pending *p = c->pending, *next; p != NULL; p = next) {
		next = p->next;
		p->done = true;
		if (p->sync) {
			wake_locked(p);
		} else {
			*tail = p;
			tail = &p->next;
		}
	}
	*tail = NULL;
	c->pending = NULL;
	mach_port_t service = c->service, remote = c->remote;
	bool remote_receive = c->kind == KIND_CLIENT && MACH_PORT_VALID(service);
	c->service = c->remote = MACH_PORT_NULL;
	if (src != NULL) {
		c->local = MACH_PORT_NULL;
	}
	mach_port_t drop_dest;
	struct queued *drop = drop_locked(c, &drop_dest);
	unwatch_locked(c);
	int unblock = 0;
	if (!c->activated) {
		unblock = c->suspended;
		c->suspended = 0;
		c->activated = true;
		c->discard = true;
	}
	os_unfair_lock_unlock(&c->lock);

	discard_queue(co, drop, drop_dest);
	if (src != NULL) {
		dispatch_source_cancel(src);
		dispatch_release(src);
	}
	_xpc_port_release(service, MACH_MSG_TYPE_PORT_SEND);
	if (remote_receive) {
		_xpc_port_release(remote, MACH_MSG_TYPE_PORT_RECEIVE);
	}
	_xpc_port_release(remote, MACH_MSG_TYPE_PORT_SEND);
	fail_pending(list, XPC_ERROR_CONNECTION_INVALID);
	for (; unblock > 0; unblock--) {
		dispatch_resume(c->q);
	}
}

static void
conn_invalidate(struct xpc_obj *co)
{
	conn_cancel(co, false);
}

static void client_drain(struct xpc_obj *co);

static void
client_reset(struct xpc_obj *co, mach_port_t remote, bool interrupt)
{
	struct conn *c = co->ext;
	mach_port_t self = mach_task_self();
	struct queued *drop = NULL;
	mach_port_t drop_dest = MACH_PORT_NULL, service = MACH_PORT_NULL;
	os_unfair_lock_lock(&c->lock);
	bool mine = !c->cancelled && MACH_PORT_VALID(remote) && c->remote == remote;
	if (mine) {
		c->remote = MACH_PORT_NULL;
		service = c->service;
		c->service = MACH_PORT_NULL;
		drop = drop_locked(c, &drop_dest);
		unwatch_locked(c);
		c->draining = interrupt;
	}
	os_unfair_lock_unlock(&c->lock);
	if (!mine) {
		return;
	}
	discard_queue(co, drop, drop_dest);
	if (MACH_PORT_VALID(service)) {
		mach_port_deallocate(self, service);
		mach_port_mod_refs(self, remote, MACH_PORT_RIGHT_RECEIVE, -1);
	}
	mach_port_deallocate(self, remote);
	if (interrupt) {
		xpc_retain(co);
		dispatch_async(c->rq, ^{
			client_drain(co);
			xpc_release(co);
		});
	}
}

static enum send_result
conn_lost(struct xpc_obj *co, mach_port_t remote)
{
	struct conn *c = co->ext;
	if (c->kind == KIND_PEER) {
		conn_invalidate(co);
		return SEND_INVALID;
	}
	client_reset(co, remote, true);
	return SEND_INTERRUPTED;
}

static void conn_flush(struct xpc_obj *co);

static void
notified(struct xpc_obj *co, mach_msg_id_t id, mach_port_t name)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	bool mine = !c->cancelled && c->watch == name;
	bool flush = mine && id == MACH_NOTIFY_SEND_POSSIBLE && c->sp_armed;
	if (flush) {
		c->sp_armed = false;
	}
	mach_port_t remote = c->remote;
	os_unfair_lock_unlock(&c->lock);
	if (flush) {
		conn_flush(co);
	} else if (mine && id == MACH_NOTIFY_DEAD_NAME) {
		conn_lost(co, remote);
	}
}

static void
notify_receive(void)
{
	mach_msg_header_t *head;
	if (_xpc_wire_receive(notify_port, MACH_RCV_TIMEOUT, 0, &head) != KERN_SUCCESS) {
		return;
	}
	mach_msg_id_t id = head->msgh_id;
	mach_port_t name = MACH_PORT_NULL;
	if (id == MACH_NOTIFY_SEND_POSSIBLE || id == MACH_NOTIFY_DEAD_NAME ||
	    id == MACH_NOTIFY_PORT_DELETED) {
		name = kernel_notification(head, id);
	}
	struct xpc_obj **hit = NULL;
	size_t nhit = 0;
	bool armed = false;
	if (MACH_PORT_VALID(name)) {
		os_unfair_lock_lock(&notes_lock);
		struct note **np = note_find(name);
		struct note *n = *np;
		armed = n != NULL && n->requests > 0;
		if (armed) {
			n->requests--;
			bool dead = false;
			size_t count = 0;
			for (struct conn *w = n->watchers; w != NULL; w = w->watch_next) {
				dead = dead || w->watch_dead;
				count++;
			}
			if (id != MACH_NOTIFY_PORT_DELETED && count > 0) {
				hit = calloc(count, sizeof(*hit));
				if (hit == NULL) {
					abort();
				}
				for (struct conn *w = n->watchers; w != NULL; w = w->watch_next) {
					hit[nhit++] = XO(xpc_retain(w->obj));
				}
			}
			if (dead && n->requests == 0 && id != MACH_NOTIFY_DEAD_NAME) {
				note_request(n, false, notify_port);
			}
			note_put(np);
		}
		os_unfair_lock_unlock(&notes_lock);
	}
	if (!armed) {
		mach_msg_destroy(head);
	}
	free(head);
	for (size_t i = 0; i < nhit; i++) {
		notified(hit[i], id, name);
		xpc_release(hit[i]);
	}
	free(hit);
	if (armed && id == MACH_NOTIFY_DEAD_NAME) {
		mach_port_deallocate(mach_task_self(), name);
	}
}

static void
note_token(struct conn *c, xpc_object_t message)
{
	struct xpc_msginfo *m = _xpc_dictionary_msginfo(message, false);
	if (c->kind == KIND_CLIENT && m != NULL && m->has_token) {
		os_unfair_lock_lock(&c->lock);
		c->token = m->token;
		c->has_token = true;
		os_unfair_lock_unlock(&c->lock);
	}
}

static void peer_drain(mach_port_t port);

static void
source_start(struct xpc_obj *co, mach_port_t port, void (^handler)(void))
{
	struct conn *c = co->ext;
	dispatch_source_t src = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, port, 0, c->rq);
	xpc_retain(co);
	dispatch_source_set_event_handler(src, handler);
	dispatch_source_set_cancel_handler(src, ^{
		if (c->kind == KIND_PEER) {
			peer_drain(port);
		}
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
		xpc_release(co);
	});
	c->source = src;
	dispatch_retain(src);
	c->recv_source = src;
	dispatch_resume(src);
}

static void
set_qlimit(mach_port_t port)
{
	mach_port_limits_t limits = { .mpl_qlimit = MACH_PORT_QLIMIT_LARGE };
	mach_port_set_attributes(mach_task_self(), port, MACH_PORT_LIMITS_INFO,
	    (mach_port_info_t)&limits, MACH_PORT_LIMITS_INFO_COUNT);
}

static void
peer_drain(mach_port_t port)
{
	mach_port_t self = mach_task_self();
	mach_port_limits_t limits = { .mpl_qlimit = MACH_PORT_QLIMIT_ZERO };
	mach_port_set_attributes(self, port, MACH_PORT_LIMITS_INFO, (mach_port_info_t)&limits,
	    MACH_PORT_LIMITS_INFO_COUNT);
	mach_port_status_t status;
	mach_msg_type_number_t count = MACH_PORT_RECEIVE_STATUS_COUNT;
	if (mach_port_get_attributes(self, port, MACH_PORT_RECEIVE_STATUS, (mach_port_info_t)&status,
	    &count) != KERN_SUCCESS) {
		return;
	}
	for (natural_t i = 0; i < status.mps_msgcount; i++) {
		mach_msg_header_t *head;
		kern_return_t kr = _xpc_wire_receive(port, MACH_RCV_TIMEOUT, 0, &head);
		if (kr == MACH_RCV_TOO_LARGE) {
			continue;
		}
		if (kr != KERN_SUCCESS) {
			return;
		}
		mach_msg_destroy(head);
		free(head);
	}
}

static bool
endpoint_receive_one(struct xpc_obj *co, bool hold)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	mach_port_t port = c->cancelled ? MACH_PORT_NULL : c->local;
	os_unfair_lock_unlock(&c->lock);
	if (!MACH_PORT_VALID(port)) {
		return false;
	}
	mach_msg_header_t *head;
	if (_xpc_wire_receive(port, MACH_RCV_TIMEOUT, 0, &head) != KERN_SUCCESS) {
		return false;
	}
	xpc_object_t msg;
	mach_port_t p0, p1;
	int r = _xpc_wire_decode(head, &msg, &p0, &p1);
	if (r == XPC_WIRE_OK) {
		_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
		if (head->msgh_id != XPC_WIRE_REPLY && head->msgh_id != XPC_WIRE_CONNECT) {
			note_token(c, msg);
			_xpc_dictionary_set_connection(msg, (xpc_connection_t)co);
			deliver_rx(co, msg, hold);
		}
		xpc_release(msg);
	} else if (r == XPC_WIRE_NOT_XPC) {
		bool gone = head->msgh_id == MACH_NOTIFY_NO_SENDERS && c->kind == KIND_PEER &&
		    from_kernel(head);
		mach_msg_destroy(head);
		if (gone) {
			conn_invalidate(co);
		}
	}
	free(head);
	return true;
}

static void
endpoint_receive(struct xpc_obj *co)
{
	endpoint_receive_one(co, true);
}

static void client_connect(struct xpc_obj *co);

static void
client_drain(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	while (endpoint_receive_one(co, false)) {
	}
	deliver(co, XPC_ERROR_CONNECTION_INTERRUPTED);
	os_unfair_lock_lock(&c->lock);
	c->draining = false;
	bool held = false;
	for (struct queued *q = c->sendq; q != NULL; q = q->next) {
		held = held || (q->barrier == NULL && !MACH_PORT_VALID(q->once));
	}
	c->connecting = held;
	os_unfair_lock_unlock(&c->lock);
	if (held) {
		client_connect(co);
	} else {
		conn_flush(co);
	}
}

static void
peer_start(struct xpc_obj *listener, mach_port_t recv, mach_port_t send, xpc_object_t first,
    bool deliver_first)
{
	struct conn *l = listener->ext;
	struct xpc_obj *po = conn_new(NULL, KIND_PEER, 0, NULL, l->rq);
	if (po == NULL) {
		_xpc_port_release(recv, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(send, MACH_MSG_TYPE_PORT_SEND);
		return;
	}
	struct conn *p = po->ext;
	p->local = recv;
	p->remote = send;
	xpc_dictionary_get_audit_token(first, &p->token);
	p->has_token = _xpc_dictionary_msginfo(first, false)->has_token;
	set_qlimit(recv);
	mach_port_t previous = MACH_PORT_NULL;
	if (mach_port_request_notification(mach_task_self(), recv, MACH_NOTIFY_NO_SENDERS, 0, recv,
	    MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous) == KERN_SUCCESS && MACH_PORT_VALID(previous)) {
		mach_port_deallocate(mach_task_self(), previous);
	}
	source_start(po, recv, ^{
		endpoint_receive(po);
	});
	deliver_rx(listener, (xpc_connection_t)po, true);
	if (deliver_first) {
		_xpc_dictionary_set_connection(first, (xpc_connection_t)po);
		deliver_rx(po, first, true);
	}
	xpc_release(po);
}

static void
listener_receive(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	mach_port_t port = c->cancelled ? MACH_PORT_NULL : c->local;
	os_unfair_lock_unlock(&c->lock);
	if (!MACH_PORT_VALID(port)) {
		return;
	}
	mach_msg_header_t *head;
	if (_xpc_wire_receive(port, MACH_RCV_TIMEOUT, 0, &head) != KERN_SUCCESS) {
		return;
	}
	xpc_object_t msg;
	mach_port_t p0, p1;
	int r = _xpc_wire_decode(head, &msg, &p0, &p1);
	if (r == XPC_WIRE_OK) {
		if (MACH_PORT_VALID(p0) && MACH_PORT_VALID(p1) && head->msgh_id != XPC_WIRE_REPLY) {
			peer_start(co, p0, p1, msg, head->msgh_id != XPC_WIRE_CONNECT);
		} else {
			_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
			_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
		}
		xpc_release(msg);
	} else if (r == XPC_WIRE_NOT_XPC) {
		mach_msg_destroy(head);
	}
	free(head);
}

static void
listener_activate(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	mach_port_t port = MACH_PORT_NULL;
	if (c->name == NULL || bootstrap_check_in(bootstrap_port, c->name, &port) != KERN_SUCCESS) {
		conn_invalidate(co);
		return;
	}
	set_qlimit(port);
	os_unfair_lock_lock(&c->lock);
	if (c->cancelled) {
		os_unfair_lock_unlock(&c->lock);
		mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
		return;
	}
	c->local = port;
	source_start(co, port, ^{
		listener_receive(co);
	});
	os_unfair_lock_unlock(&c->lock);
}

static kern_return_t
client_install_locked(struct xpc_obj *co, mach_port_t service)
{
	struct conn *c = co->ext;
	mach_port_t self = mach_task_self();
	if (!MACH_PORT_VALID(c->local)) {
		mach_port_options_t opts = {
			.flags = MPO_QLIMIT,
			.mpl = { .mpl_qlimit = MACH_PORT_QLIMIT_LARGE },
		};
		mach_port_t port = MACH_PORT_NULL;
		kern_return_t kr = mach_port_construct(self, &opts, 0, &port);
		if (kr != KERN_SUCCESS) {
			return kr;
		}
		c->local = port;
		source_start(co, port, ^{
			endpoint_receive(co);
		});
	}
	mach_port_options_t popts = {
		.flags = MPO_QLIMIT | MPO_INSERT_SEND_RIGHT,
		.mpl = { .mpl_qlimit = MACH_PORT_QLIMIT_LARGE },
	};
	mach_port_t remote = MACH_PORT_NULL;
	kern_return_t kr = mach_port_construct(self, &popts, 0, &remote);
	if (kr != KERN_SUCCESS) {
		return kr;
	}
	c->service = service;
	c->remote = remote;
	return KERN_SUCCESS;
}

static void address_early_locked(struct conn *c, struct queued **dropped);
static void drop_refused(struct conn *c, struct queued *dropped);

static void
client_connect(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	uint64_t flags = c->privileged ? BOOTSTRAP_PRIVILEGED_SERVER : 0;
	os_unfair_lock_unlock(&c->lock);
	mach_port_t service = MACH_PORT_NULL;
	kern_return_t kr = bootstrap_look_up2(bootstrap_port, c->name, &service, 0, flags);
	os_unfair_lock_lock(&c->lock);
	c->connecting = false;
	bool cancelled = c->cancelled;
	struct queued *dropped = NULL;
	if (!cancelled && kr == KERN_SUCCESS) {
		kr = client_install_locked(co, service);
		if (kr == KERN_SUCCESS) {
			service = MACH_PORT_NULL;
			address_early_locked(c, &dropped);
		}
	}
	os_unfair_lock_unlock(&c->lock);
	drop_refused(c, dropped);
	if (MACH_PORT_VALID(service)) {
		mach_port_deallocate(mach_task_self(), service);
	}
	if (cancelled) {
		return;
	}
	if (kr != KERN_SUCCESS) {
		conn_invalidate(co);
	} else {
		conn_flush(co);
	}
}

static bool
too_many_for_handshake(struct queued *q)
{
	return q->msg.nports + 2 > XPC_WIRE_PORTS_MAX;
}

static struct queued *
connect_new_locked(struct conn *c, mach_port_t service, mach_port_t receive)
{
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	struct xpc_wire_out out = {
		.dest = service,
		.dest_type = MACH_MSG_TYPE_COPY_SEND,
		.port0 = receive,
		.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE,
		.port1 = c->local,
		.port1_type = MACH_MSG_TYPE_MAKE_SEND,
		.id = XPC_WIRE_CONNECT,
	};
	xpc_object_t empty = xpc_dictionary_create(NULL, NULL, 0);
	kern_return_t kr = _xpc_wire_prepare(empty, &out, &q->msg);
	if (kr == KERN_SUCCESS) {
		kr = _xpc_wire_hold(&q->msg);
	}
	xpc_release(empty);
	if (kr != KERN_SUCCESS) {
		q->msg.receive = MACH_PORT_NULL;
		queued_free(q);
		return NULL;
	}
	q->service = service;
	return q;
}

static void
unlink_locked(struct conn *c, struct queued **link, struct queued **dropped)
{
	struct queued *q = *link;
	*link = q->next;
	if (*link == NULL) {
		c->sendq_tail = link;
	}
	q->next = *dropped;
	*dropped = q;
}

static void
drop_refused(struct conn *c, struct queued *dropped)
{
	while (dropped != NULL) {
		struct queued *next = dropped->next;
		bad_message(c, KERN_RESOURCE_SHORTAGE);
		queued_free(dropped);
		dropped = next;
	}
}

static void
handshake_restore_locked(struct conn *c, struct queued *q, struct queued **dropped)
{
	struct queued **link = &c->sendq;
	struct queued *next, *cq = NULL;
	for (;;) {
		while (*link != NULL && ((*link)->barrier != NULL || MACH_PORT_VALID((*link)->once))) {
			link = &(*link)->next;
		}
		next = *link;
		if (next == NULL || !too_many_for_handshake(next) ||
		    (cq = connect_new_locked(c, q->service, q->msg.receive)) != NULL) {
			break;
		}
		unlink_locked(c, link, dropped);
	}
	if (cq != NULL) {
		cq->next = next;
		*link = cq;
	} else if (next != NULL) {
		struct xpc_wire_out out = {
			.dest = q->service,
			.dest_type = MACH_MSG_TYPE_COPY_SEND,
			.port0 = q->msg.receive,
			.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE,
			.port1 = c->local,
			.port1_type = MACH_MSG_TYPE_MAKE_SEND,
		};
		_xpc_wire_address(&next->msg, &out);
		next->service = q->service;
	} else {
		c->service = q->service;
	}
	q->service = q->msg.receive = MACH_PORT_NULL;
}

static void
conn_flush(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	bool lost = false;
	mach_port_t remote = MACH_PORT_NULL;
	os_unfair_lock_lock(&c->lock);
	if (c->flushing || c->sp_armed) {
		os_unfair_lock_unlock(&c->lock);
		return;
	}
	c->flushing = true;
	while (c->sendq != NULL && !lost && c->suspended == 0 && !c->draining && !c->connecting) {
		struct queued *q = c->sendq;
		c->sendq = q->next;
		if (c->sendq == NULL) {
			c->sendq_tail = &c->sendq;
		}
		if (q->barrier != NULL) {
			mach_port_t spare = MACH_PORT_NULL;
			if (c->sendq == NULL) {
				spare = c->sendq_dest;
				c->sendq_dest = MACH_PORT_NULL;
			}
			os_unfair_lock_unlock(&c->lock);
			if (MACH_PORT_VALID(spare)) {
				mach_port_deallocate(mach_task_self(), spare);
			}
			barrier_post(co, q, true);
			return;
		}
		unsigned epoch = c->epoch;
		bool handshake = MACH_PORT_VALID(q->service);
		bool connect = handshake && q->msg.head->msgh_id == XPC_WIRE_CONNECT;
		bool once = MACH_PORT_VALID(q->once);
		os_unfair_lock_unlock(&c->lock);
		kern_return_t kr = _xpc_wire_try(&q->msg);
		kern_return_t base = kr & ~MACH_MSG_MASK;
		os_unfair_lock_lock(&c->lock);
		mach_port_t spare = MACH_PORT_NULL;
		kern_return_t bad = KERN_SUCCESS;
		struct queued *dropped = NULL;
		if (once && (kr == KERN_SUCCESS || !_xpc_wire_kept(kr))) {
			q->once = MACH_PORT_NULL;
		}
		if (c->epoch != epoch) {
			spare = c->flush_dest;
			c->flush_dest = MACH_PORT_NULL;
		} else if (once) {
			if (c->sendq == NULL) {
				spare = c->sendq_dest;
				c->sendq_dest = MACH_PORT_NULL;
			}
			if (refused(kr)) {
				bad = kr;
			}
		} else if (kr == MACH_SEND_TIMED_OUT) {
			q->next = c->sendq;
			c->sendq = q;
			if (q->next == NULL) {
				c->sendq_tail = &q->next;
			}
			if (watch_locked(c, q->msg.head->msgh_remote_port, true) == KERN_SUCCESS) {
				c->sp_armed = true;
			} else {
				lost = true;
				remote = c->remote;
			}
			break;
		} else if (handshake && !connect && kr != KERN_SUCCESS && kr != MACH_SEND_INVALID_DEST &&
		    base != MACH_SEND_TIMED_OUT && base != MACH_SEND_NO_BUFFER &&
		    MACH_PORT_VALID(q->msg.receive)) {
			handshake_restore_locked(c, q, &dropped);
			bad = kr;
		} else {
			if (c->sendq == NULL) {
				spare = c->sendq_dest;
				c->sendq_dest = MACH_PORT_NULL;
			}
			if (refused(kr) && !connect) {
				bad = kr;
			} else if (kr != KERN_SUCCESS || (handshake &&
			    watch_locked(c, c->remote, false) != KERN_SUCCESS)) {
				lost = true;
				remote = c->remote;
			}
		}
		os_unfair_lock_unlock(&c->lock);
		if (bad != KERN_SUCCESS) {
			bad_message(c, bad);
		}
		drop_refused(c, dropped);
		queued_free(q);
		if (MACH_PORT_VALID(spare)) {
			mach_port_deallocate(mach_task_self(), spare);
		}
		os_unfair_lock_lock(&c->lock);
	}
	mach_port_t spare = c->flush_dest;
	c->flush_dest = MACH_PORT_NULL;
	c->flushing = false;
	os_unfair_lock_unlock(&c->lock);
	if (MACH_PORT_VALID(spare)) {
		mach_port_deallocate(mach_task_self(), spare);
	}
	if (lost) {
		conn_lost(co, remote);
	}
}

static void
send_end(struct xpc_obj *co)
{
	struct conn *c = co->ext;
	os_unfair_lock_lock(&c->lock);
	c->flushing = false;
	os_unfair_lock_unlock(&c->lock);
	conn_flush(co);
}

static enum send_result
conn_queue(struct xpc_obj *co, struct queued *q, unsigned epoch, bool owner, bool *stale)
{
	struct conn *c = co->ext;
	mach_port_t dest = q->msg.head->msgh_remote_port;
	os_unfair_lock_lock(&c->lock);
	bool cancelled = c->cancelled;
	bool ok = !cancelled && c->epoch == epoch;
	if (ok) {
		if (!MACH_PORT_VALID(c->sendq_dest)) {
			c->sendq_dest = dest;
			dest = MACH_PORT_NULL;
		}
		if (owner) {
			q->next = c->sendq;
			c->sendq = q;
			if (q->next == NULL) {
				c->sendq_tail = &q->next;
			}
		} else {
			q->next = NULL;
			*c->sendq_tail = q;
			c->sendq_tail = &q->next;
		}
	}
	if (owner) {
		c->flushing = false;
	}
	os_unfair_lock_unlock(&c->lock);
	if (MACH_PORT_VALID(dest)) {
		mach_port_deallocate(mach_task_self(), dest);
	}
	*stale = !ok;
	if (!ok) {
		queued_free(q);
	}
	conn_flush(co);
	if (!ok) {
		return cancelled || c->kind == KIND_PEER ? SEND_INVALID : SEND_INTERRUPTED;
	}
	return SEND_OK;
}

static enum send_result
conn_sent(struct xpc_obj *co, struct queued *q, kern_return_t kr)
{
	mach_port_t remote = q->msg.head->msgh_remote_port;
	kern_return_t base = kr & ~MACH_MSG_MASK;
	queued_free(q);
	mach_port_deallocate(mach_task_self(), remote);
	if (kr == KERN_SUCCESS) {
		return SEND_OK;
	}
	if (base == MACH_SEND_TIMED_OUT || base == MACH_SEND_NO_BUFFER || kr == MACH_SEND_INVALID_DEST) {
		return conn_lost(co, remote);
	}
	return bad_message(co->ext, kr);
}

static enum send_result
queue_send(struct xpc_obj *co, xpc_object_t message, const struct xpc_wire_out *out,
    unsigned epoch, bool owner, bool *stale)
{
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	enum send_result r = SEND_BAD_MESSAGE;
	kern_return_t kr = _xpc_wire_prepare(message, out, &q->msg);
	if (kr != KERN_SUCCESS) {
		free(q);
		mach_port_deallocate(mach_task_self(), out->dest);
		r = bad_message(co->ext, kr);
	} else if (owner && (kr = _xpc_wire_try(&q->msg)) != MACH_SEND_TIMED_OUT) {
		*stale = kr != KERN_SUCCESS && !_xpc_wire_kept(kr);
		r = conn_sent(co, q, kr);
	} else if ((kr = _xpc_wire_hold(&q->msg)) != KERN_SUCCESS) {
		queued_free(q);
		mach_port_deallocate(mach_task_self(), out->dest);
		r = bad_message(co->ext, kr);
	} else {
		return conn_queue(co, q, epoch, owner, stale);
	}
	if (owner) {
		send_end(co);
	}
	return r;
}

static void
ordinary_locked(struct conn *c, struct queued *q)
{
	struct xpc_wire_out out = { .dest = c->remote, .dest_type = MACH_MSG_TYPE_COPY_SEND };
	_xpc_wire_address(&q->msg, &out);
	if (!MACH_PORT_VALID(c->sendq_dest) &&
	    mach_port_mod_refs(mach_task_self(), c->remote, MACH_PORT_RIGHT_SEND, 1) == KERN_SUCCESS) {
		c->sendq_dest = c->remote;
	}
}

static enum send_result
handshake_send_locked(struct xpc_obj *co, xpc_object_t message, struct xpc_wire_out *out)
{
	struct conn *c = co->ext;
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	out->dest = c->service;
	out->dest_type = MACH_MSG_TYPE_COPY_SEND;
	out->port0 = c->remote;
	out->port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out->port1 = c->local;
	out->port1_type = MACH_MSG_TYPE_MAKE_SEND;
	kern_return_t kr = _xpc_wire_prepare(message, out, &q->msg);
	if (kr == KERN_SUCCESS) {
		kr = _xpc_wire_hold(&q->msg);
	}
	if (kr != KERN_SUCCESS) {
		q->msg.receive = MACH_PORT_NULL;
		os_unfair_lock_unlock(&c->lock);
		queued_free(q);
		return bad_message(c, kr);
	}
	if (too_many_for_handshake(q)) {
		struct queued *cq = connect_new_locked(c, c->service, c->remote);
		if (cq == NULL) {
			q->msg.receive = MACH_PORT_NULL;
			os_unfair_lock_unlock(&c->lock);
			queued_free(q);
			return bad_message(c, KERN_RESOURCE_SHORTAGE);
		}
		*c->sendq_tail = cq;
		c->sendq_tail = &cq->next;
		ordinary_locked(c, q);
	} else {
		q->service = c->service;
	}
	c->service = MACH_PORT_NULL;
	*c->sendq_tail = q;
	c->sendq_tail = &q->next;
	os_unfair_lock_unlock(&c->lock);
	conn_flush(co);
	return SEND_OK;
}

static enum send_result
early_send_locked(struct xpc_obj *co, xpc_object_t message, struct xpc_wire_out *out)
{
	struct conn *c = co->ext;
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	out->dest = MACH_PORT_NULL;
	out->dest_type = MACH_MSG_TYPE_COPY_SEND;
	kern_return_t kr = _xpc_wire_prepare(message, out, &q->msg);
	if (kr == KERN_SUCCESS) {
		kr = _xpc_wire_hold(&q->msg);
	}
	if (kr != KERN_SUCCESS) {
		os_unfair_lock_unlock(&c->lock);
		queued_free(q);
		return bad_message(c, kr);
	}
	*c->sendq_tail = q;
	c->sendq_tail = &q->next;
	os_unfair_lock_unlock(&c->lock);
	return SEND_OK;
}

static enum send_result
reply_queue_locked(struct xpc_obj *co, xpc_object_t message, struct xpc_wire_out *out,
    mach_port_t rp)
{
	struct conn *c = co->ext;
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	kern_return_t kr = _xpc_wire_prepare(message, out, &q->msg);
	if (kr == KERN_SUCCESS) {
		kr = _xpc_wire_hold(&q->msg);
	}
	if (kr != KERN_SUCCESS) {
		os_unfair_lock_unlock(&c->lock);
		queued_free(q);
		mach_port_deallocate(mach_task_self(), rp);
		return bad_message(c, kr);
	}
	q->once = rp;
	*c->sendq_tail = q;
	c->sendq_tail = &q->next;
	os_unfair_lock_unlock(&c->lock);
	conn_flush(co);
	return SEND_OK;
}

static void
address_early_locked(struct conn *c, struct queued **dropped)
{
	bool others = false;
	struct queued **link = &c->sendq;
	while (*link != NULL) {
		struct queued *q = *link;
		if (q->barrier != NULL || MACH_PORT_VALID(q->once)) {
			link = &q->next;
			continue;
		}
		struct xpc_wire_out out = { .dest = c->remote, .dest_type = MACH_MSG_TYPE_COPY_SEND };
		if (MACH_PORT_VALID(c->service) && too_many_for_handshake(q)) {
			struct queued *cq = connect_new_locked(c, c->service, c->remote);
			if (cq == NULL) {
				unlink_locked(c, link, dropped);
				continue;
			}
			cq->next = q;
			*link = cq;
			c->service = MACH_PORT_NULL;
			others = true;
		} else if (MACH_PORT_VALID(c->service)) {
			out.dest = c->service;
			out.port0 = c->remote;
			out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
			out.port1 = c->local;
			out.port1_type = MACH_MSG_TYPE_MAKE_SEND;
			q->service = c->service;
			c->service = MACH_PORT_NULL;
		} else {
			others = true;
		}
		_xpc_wire_address(&q->msg, &out);
		link = &q->next;
	}
	if (others && mach_port_mod_refs(mach_task_self(), c->remote, MACH_PORT_RIGHT_SEND, 1) ==
	    KERN_SUCCESS) {
		c->sendq_dest = c->remote;
	}
}

static enum send_result
conn_send(struct xpc_obj *co, xpc_object_t message, mach_msg_id_t id, mach_port_t reply,
    bool *stale)
{
	struct conn *c = co->ext;
	mach_port_t self = mach_task_self();
	struct xpc_wire_out out = {
		.reply = reply,
		.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE,
		.id = id,
	};
	bool ignored;
	if (stale == NULL) {
		stale = &ignored;
	}
	*stale = false;

	mach_port_t rp = _xpc_dictionary_take_reply(message);
	if (MACH_PORT_VALID(rp)) {
		out.dest = rp;
		out.dest_type = MACH_MSG_TYPE_MOVE_SEND_ONCE;
		out.id = XPC_WIRE_REPLY;
		os_unfair_lock_lock(&c->lock);
		if (c->cancelled) {
			os_unfair_lock_unlock(&c->lock);
			mach_port_deallocate(self, rp);
			return SEND_INVALID;
		}
		if (MACH_PORT_VALID(reply)) {
			return reply_queue_locked(co, message, &out, rp);
		}
		os_unfair_lock_unlock(&c->lock);
		kern_return_t kr = _xpc_wire_send(message, &out);
		if (kr != KERN_SUCCESS && _xpc_wire_kept(kr)) {
			mach_port_deallocate(self, rp);
		}
		return kr == KERN_SUCCESS ? SEND_OK : refused(kr) ? bad_message(c, kr) : SEND_INTERRUPTED;
	}

	os_unfair_lock_lock(&c->lock);
	if (c->cancelled || c->kind == KIND_NONE || c->kind == KIND_LISTENER) {
		os_unfair_lock_unlock(&c->lock);
		return SEND_INVALID;
	}
	if (c->kind == KIND_CLIENT && (!c->activated || c->draining || c->connecting)) {
		return early_send_locked(co, message, &out);
	}
	if (c->kind == KIND_CLIENT && !MACH_PORT_VALID(c->remote)) {
		c->connecting = true;
		enum send_result r = early_send_locked(co, message, &out);
		client_connect(co);
		return r;
	}
	if (c->kind == KIND_CLIENT && MACH_PORT_VALID(c->service)) {
		return handshake_send_locked(co, message, &out);
	}
	mach_port_t remote = c->remote;
	if (mach_port_mod_refs(self, remote, MACH_PORT_RIGHT_SEND, 1) != KERN_SUCCESS) {
		os_unfair_lock_unlock(&c->lock);
		return conn_lost(co, remote);
	}
	out.dest = remote;
	out.dest_type = MACH_MSG_TYPE_COPY_SEND;
	unsigned epoch = c->epoch;
	bool owner = c->sendq == NULL && !c->flushing && c->suspended == 0;
	if (owner) {
		c->flushing = true;
	}
	os_unfair_lock_unlock(&c->lock);
	return queue_send(co, message, &out, epoch, owner, stale);
}

static xpc_object_t
send_error(enum send_result r)
{
	return r == SEND_INVALID ? XPC_ERROR_CONNECTION_INVALID : XPC_ERROR_CONNECTION_INTERRUPTED;
}

xpc_connection_t
xpc_connection_create(const char *name, dispatch_queue_t targetq)
{
	return (xpc_connection_t)conn_new(name, name ? KIND_CLIENT : KIND_NONE, 0, targetq, NULL);
}

xpc_connection_t
xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags)
{
	enum kind kind = name == NULL ? KIND_NONE :
	    (flags & XPC_CONNECTION_MACH_SERVICE_LISTENER) ? KIND_LISTENER : KIND_CLIENT;
	return (xpc_connection_t)conn_new(name, kind, flags, targetq, NULL);
}

xpc_connection_t
xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint)
{
	(void)endpoint;
	return (xpc_connection_t)conn_new(NULL, KIND_NONE, 0, NULL, NULL);
}

void
xpc_connection_set_legacy(xpc_connection_t connection)
{
	(void)connection;
}

void
xpc_connection_set_privileged(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c != NULL) {
		os_unfair_lock_lock(&c->lock);
		c->privileged = true;
		os_unfair_lock_unlock(&c->lock);
	}
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
	dispatch_queue_t old = c->target;
	c->target = targetq;
	os_unfair_lock_unlock(&c->lock);
	dispatch_set_target_queue(c->q, targetq);
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
xpc_connection_resume(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	struct xpc_obj *co = XO(connection);
	os_unfair_lock_lock(&c->lock);
	bool resume = c->suspended > 0;
	bool first = !c->activated;
	if (resume) {
		c->suspended--;
	}
	c->activated = true;
	bool connect = first && c->kind == KIND_CLIENT && !c->cancelled;
	if (connect) {
		c->connecting = true;
	}
	os_unfair_lock_unlock(&c->lock);
	if (resume) {
		dispatch_resume(c->q);
	}
	if (first && c->kind == KIND_LISTENER) {
		listener_activate(co);
	} else if (first && c->kind == KIND_NONE) {
		conn_invalidate(co);
	} else if (connect) {
		client_connect(co);
	} else if (c->kind == KIND_CLIENT || c->kind == KIND_PEER) {
		conn_flush(co);
	}
}

void
xpc_connection_activate(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	os_unfair_lock_lock(&c->lock);
	bool first = !c->activated;
	os_unfair_lock_unlock(&c->lock);
	if (first) {
		xpc_connection_resume(connection);
	}
}

void
xpc_connection_suspend(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	os_unfair_lock_lock(&c->lock);
	c->suspended++;
	os_unfair_lock_unlock(&c->lock);
	dispatch_suspend(c->q);
}

void
xpc_connection_cancel(xpc_connection_t connection)
{
	if (CONN(connection) != NULL) {
		conn_cancel(XO(connection), true);
	}
}

void
xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message)
{
	if (CONN(connection) != NULL) {
		conn_send(XO(connection), message, XPC_WIRE_MESSAGE, MACH_PORT_NULL, NULL);
	}
}

void
xpc_connection_send_barrier(xpc_connection_t connection, dispatch_block_t barrier)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	struct xpc_obj *co = XO(connection);
	os_unfair_lock_lock(&c->lock);
	if (c->cancelled || c->kind == KIND_NONE || c->kind == KIND_LISTENER) {
		os_unfair_lock_unlock(&c->lock);
		dispatch_async(c->q, barrier);
		return;
	}
	struct queued *q = calloc(1, sizeof(*q));
	if (q == NULL) {
		abort();
	}
	q->barrier = Block_copy(barrier);
	xpc_retain(co);
	*c->sendq_tail = q;
	c->sendq_tail = &q->next;
	os_unfair_lock_unlock(&c->lock);
	conn_flush(co);
}

void
xpc_connection_send_message_with_reply(xpc_connection_t connection, xpc_object_t message,
    dispatch_queue_t replyq, xpc_handler_t handler)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return;
	}
	dispatch_queue_t q = replyq ? replyq : c->q;
	if (handler == NULL) {
		conn_send(XO(connection), message, XPC_WIRE_MESSAGE, MACH_PORT_NULL, NULL);
		return;
	}
	xpc_handler_t h = Block_copy(handler);
	mach_port_options_t opts = { .flags = 0 };
	mach_port_t port = MACH_PORT_NULL;
	if (c->rq == NULL || mach_port_construct(mach_task_self(), &opts, 0, &port) != KERN_SUCCESS) {
		dispatch_async(q, ^{
			h(XPC_ERROR_CONNECTION_INVALID);
			Block_release(h);
		});
		return;
	}
	struct pending *p = calloc(1, sizeof(*p));
	if (p == NULL) {
		abort();
	}
	p->conn = XO(xpc_retain(connection));
	p->port = port;
	p->handler = h;
	p->replyq = q;
	dispatch_retain(q);
	p->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, port, 0, c->rq);
	dispatch_source_set_event_handler(p->source, ^{
		pending_receive(p);
	});
	dispatch_source_set_cancel_handler(p->source, ^{
		mach_port_mod_refs(mach_task_self(), p->port, MACH_PORT_RIGHT_RECEIVE, -1);
		dispatch_release(p->source);
		Block_release(p->handler);
		dispatch_release(p->replyq);
		xpc_release(p->conn);
		free(p);
	});

	os_unfair_lock_lock(&c->lock);
	p->next = c->pending;
	c->pending = p;
	os_unfair_lock_unlock(&c->lock);

	enum send_result r = conn_send(XO(connection), message, XPC_WIRE_REQUEST, port, NULL);
	if (r != SEND_OK) {
		pending_finish(p, send_error(r));
	}
	dispatch_resume(p->source);
}

xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t connection, xpc_object_t message)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return XPC_ERROR_CONNECTION_INVALID;
	}
	mach_port_t port = mig_get_reply_port();
	struct pending w = { .conn = XO(connection), .port = port, .sync = true };
	os_unfair_lock_lock(&c->lock);
	w.next = c->pending;
	c->pending = &w;
	os_unfair_lock_unlock(&c->lock);
	bool stale;
	enum send_result r = conn_send(XO(connection), message, XPC_WIRE_REQUEST, port, &stale);
	mach_msg_header_t *head = NULL;
	kern_return_t kr = r == SEND_OK ? _xpc_wire_receive(port, 0, 0, &head) : KERN_SUCCESS;
	os_unfair_lock_lock(&c->lock);
	bool woken = w.done;
	if (!woken) {
		pending_unlink_locked(c, &w);
	}
	os_unfair_lock_unlock(&c->lock);
	if (woken) {
		if (head != NULL) {
			mach_msg_destroy(head);
			free(head);
		}
		mig_dealloc_reply_port(port);
		return XPC_ERROR_CONNECTION_INVALID;
	}
	if (r != SEND_OK) {
		if (stale) {
			mig_dealloc_reply_port(port);
		}
		return send_error(r);
	}
	if (kr != KERN_SUCCESS) {
		mig_dealloc_reply_port(port);
		return XPC_ERROR_CONNECTION_INTERRUPTED;
	}
	xpc_object_t reply;
	mach_port_t p0, p1;
	int d = _xpc_wire_decode(head, &reply, &p0, &p1);
	mach_msg_id_t id = head->msgh_id;
	if (d == XPC_WIRE_NOT_XPC) {
		mach_msg_destroy(head);
	}
	free(head);
	if (d != XPC_WIRE_OK) {
		return XPC_ERROR_CONNECTION_INTERRUPTED;
	}
	_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
	_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
	if (id != XPC_WIRE_REPLY) {
		xpc_release(reply);
		return XPC_ERROR_CONNECTION_INTERRUPTED;
	}
	note_token(c, reply);
	_xpc_dictionary_set_connection(reply, connection);
	return reply;
}

const char *
xpc_connection_get_name(xpc_connection_t connection)
{
	struct conn *c = CONN(connection);
	return c ? c->name : NULL;
}

static bool
conn_token(xpc_connection_t connection, audit_token_t *token)
{
	struct conn *c = CONN(connection);
	if (c == NULL) {
		return false;
	}
	os_unfair_lock_lock(&c->lock);
	bool has = c->has_token;
	*token = c->token;
	os_unfair_lock_unlock(&c->lock);
	return has;
}

void
xpc_connection_get_audit_token(xpc_connection_t connection, audit_token_t *token)
{
	if (!conn_token(connection, token)) {
		memset(token, 0xff, sizeof(*token));
	}
}

uid_t
xpc_connection_get_euid(xpc_connection_t connection)
{
	audit_token_t t;
	return conn_token(connection, &t) ? (uid_t)t.val[1] : (uid_t)-1;
}

gid_t
xpc_connection_get_egid(xpc_connection_t connection)
{
	audit_token_t t;
	return conn_token(connection, &t) ? (gid_t)t.val[2] : (gid_t)-1;
}

pid_t
xpc_connection_get_pid(xpc_connection_t connection)
{
	audit_token_t t;
	return conn_token(connection, &t) ? (pid_t)t.val[5] : 0;
}

au_asid_t
xpc_connection_get_asid(xpc_connection_t connection)
{
	audit_token_t t;
	return conn_token(connection, &t) ? (au_asid_t)t.val[6] : 0;
}

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
