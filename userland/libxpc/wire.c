/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "internal.h"

#include <os/log.h>
#include <stdlib.h>
#include <string.h>

#define WIRE_MAGIC      0x43505850u
#define WIRE_VERSION    2u
#define WIRE_INLINE_MAX 16384u
#define WIRE_DEPTH_MAX  512
#define WIRE_RCV_SIZE   (WIRE_INLINE_MAX + 1024u)
#define WIRE_TRAILER    (MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | \
	MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT))

enum {
	T_NULL = 1,
	T_BOOL,
	T_INT64,
	T_UINT64,
	T_DOUBLE,
	T_DATE,
	T_DATA,
	T_STRING,
	T_UUID,
	T_ARRAY,
	T_DICTIONARY,
	T_FD,
	T_MACH_SEND,
};

static const xpc_type_t wire_types[] = {
	[T_NULL] = XPC_TYPE_NULL,
	[T_BOOL] = XPC_TYPE_BOOL,
	[T_INT64] = XPC_TYPE_INT64,
	[T_UINT64] = XPC_TYPE_UINT64,
	[T_DOUBLE] = XPC_TYPE_DOUBLE,
	[T_DATE] = XPC_TYPE_DATE,
	[T_DATA] = XPC_TYPE_DATA,
	[T_STRING] = XPC_TYPE_STRING,
	[T_UUID] = XPC_TYPE_UUID,
	[T_ARRAY] = XPC_TYPE_ARRAY,
	[T_DICTIONARY] = XPC_TYPE_DICTIONARY,
	[T_FD] = XPC_TYPE_FD,
	[T_MACH_SEND] = XPC_TYPE_MACH_SEND,
};

struct wire_hdr {
	uint32_t magic;
	uint32_t version;
	uint32_t nports;
	uint32_t ool;
	uint32_t length;
	uint32_t fixed;
};

static uint64_t wire_len_max = UINT32_MAX;
static uint32_t wire_array_max = 1u << 24;

static void __attribute__((noreturn))
too_large(const char *what)
{
	os_log_fault(OS_LOG_DEFAULT, "libxpc: a message has too many %s to send", what);
	__builtin_trap();
}

#define HEIGHT_INF (WIRE_DEPTH_MAX + 1)

struct shape {
	const struct xpc_obj *o;
	uint32_t height;
	bool big;
	bool busy;
	size_t index, low;
};

struct memo {
	struct shape *v;
	size_t cap, n;
};

static size_t
memo_hash(const struct xpc_obj *o, size_t cap)
{
	uint64_t h = (uint64_t)(uintptr_t)o >> 4;
	return (size_t)((h * 0x9e3779b97f4a7c15ull) >> (64 - __builtin_ctzll(cap)));
}

static struct shape *
memo_find(struct memo *m, const struct xpc_obj *o)
{
	if (m->cap == 0) {
		return NULL;
	}
	for (size_t i = memo_hash(o, m->cap);; i = (i + 1) & (m->cap - 1)) {
		if (m->v[i].o == NULL) {
			return NULL;
		}
		if (m->v[i].o == o) {
			return &m->v[i];
		}
	}
}

static struct shape *
memo_add(struct memo *m, const struct xpc_obj *o)
{
	if (2 * (m->n + 1) > m->cap) {
		size_t cap = m->cap ? 2 * m->cap : 64;
		struct shape *v = calloc(cap, sizeof(*v));
		if (v == NULL) {
			return NULL;
		}
		for (size_t i = 0; i < m->cap; i++) {
			if (m->v[i].o != NULL) {
				size_t j = memo_hash(m->v[i].o, cap);
				while (v[j].o != NULL) {
					j = (j + 1) & (cap - 1);
				}
				v[j] = m->v[i];
			}
		}
		free(m->v);
		m->v = v;
		m->cap = cap;
	}
	size_t i = memo_hash(o, m->cap);
	while (m->v[i].o != NULL) {
		i = (i + 1) & (m->cap - 1);
	}
	m->v[i].o = o;
	m->n++;
	return &m->v[i];
}

enum {
	OVER_BYTES = 1,
	OVER_PORTS,
};

struct enc {
	uint8_t *buf;
	uint64_t len;
	size_t cap;
	mach_port_t *ports;
	size_t nports, pcap;
	bool bad;
	int over;
	struct memo shapes, seen;
};

static void
put(struct enc *e, const void *p, size_t n)
{
	if (e->bad || e->over) {
		return;
	}
	if (e->len > wire_len_max || n > wire_len_max - e->len) {
		e->over = OVER_BYTES;
		return;
	}
	if (e->len + n > e->cap) {
		size_t cap = e->cap ? e->cap : 256;
		while (cap < e->len + n) {
			cap *= 2;
		}
		uint8_t *b = realloc(e->buf, cap);
		if (b == NULL) {
			e->bad = true;
			return;
		}
		e->buf = b;
		e->cap = cap;
	}
	memcpy(e->buf + e->len, p, n);
	e->len += n;
}

static void
put32(struct enc *e, uint32_t v)
{
	put(e, &v, sizeof(v));
}

static void
put_port(struct enc *e, mach_port_t port)
{
	if (e->bad || e->over) {
		return;
	}
	if (e->nports >= XPC_WIRE_PORTS_MAX) {
		e->over = OVER_PORTS;
		return;
	}
	if (e->nports == e->pcap) {
		size_t cap = e->pcap ? 2 * e->pcap : 4;
		mach_port_t *p = realloc(e->ports, cap * sizeof(*p));
		if (p == NULL) {
			e->bad = true;
			return;
		}
		e->ports = p;
		e->pcap = cap;
	}
	put32(e, (uint32_t)e->nports);
	e->ports[e->nports++] = port;
}

static bool
container(const struct xpc_obj *o)
{
	return o != NULL && (o->type == XPC_TYPE_ARRAY || o->type == XPC_TYPE_DICTIONARY);
}

static size_t
children(const struct xpc_obj *o)
{
	return o->type == XPC_TYPE_ARRAY ? o->arr.n : o->dict.n;
}

static const struct xpc_obj *
child(const struct xpc_obj *o, size_t i)
{
	return o->type == XPC_TYPE_ARRAY ? o->arr.v[i] : o->dict.vals[i];
}

static void *
grow(void *v, size_t *cap, size_t n, size_t size)
{
	if (n < *cap) {
		return v;
	}
	size_t c = 2 * *cap;
	void *b = realloc(v, c * size);
	if (b != NULL) {
		*cap = c;
	}
	return b;
}

static void
settle(struct enc *e, struct shape *s)
{
	const struct xpc_obj *o = s->o;
	bool array = o->type == XPC_TYPE_ARRAY;
	size_t count = children(o);
	uint32_t height = 0;
	bool big = array && count >= wire_array_max;
	for (size_t i = 0; i < count; i++) {
		const struct xpc_obj *c = child(o, i);
		if (c == o) {
			height = HEIGHT_INF;
		} else if (array && container(c)) {
			struct shape *t = memo_find(&e->shapes, c);
			height = t->height > height ? t->height : height;
			big = big || t->big;
		}
	}
	if (array && count > 0) {
		height = height >= HEIGHT_INF - 1 ? HEIGHT_INF : height + 1;
	}
	s->height = height;
	s->big = big;
}

struct frame {
	const struct xpc_obj *o;
	size_t next;
};

static bool
measure(struct enc *e, const struct xpc_obj *root)
{
	size_t n = 0, cap = 64, m = 0, mcap = 64, index = 0;
	struct frame *st = malloc(cap * sizeof(*st));
	const struct xpc_obj **scc = malloc(mcap * sizeof(*scc));
	const struct xpc_obj *open = root;
	bool ok = st != NULL && scc != NULL;
	while (ok) {
		if (open != NULL) {
			void *b = grow(st, &cap, n, sizeof(*st));
			if (b == NULL) {
				ok = false;
				break;
			}
			st = b;
			b = grow(scc, &mcap, m, sizeof(*scc));
			if (b == NULL) {
				ok = false;
				break;
			}
			scc = b;
			struct shape *s = memo_add(&e->shapes, open);
			if (s == NULL) {
				ok = false;
				break;
			}
			s->index = s->low = index++;
			s->busy = true;
			scc[m++] = open;
			st[n++] = (struct frame){ open, 0 };
			open = NULL;
		}
		struct frame *f = &st[n - 1];
		if (f->next < children(f->o)) {
			const struct xpc_obj *c = child(f->o, f->next++);
			if (!container(c)) {
				continue;
			}
			struct shape *s = memo_find(&e->shapes, c);
			if (s == NULL) {
				open = c;
			} else if (s->busy) {
				struct shape *p = memo_find(&e->shapes, f->o);
				p->low = s->index < p->low ? s->index : p->low;
			}
			continue;
		}
		struct shape *s = memo_find(&e->shapes, f->o);
		n--;
		if (s->low == s->index) {
			size_t k = m;
			do {
				k--;
			} while (scc[k] != f->o);
			for (size_t i = k; i < m; i++) {
				struct shape *t = memo_find(&e->shapes, scc[i]);
				t->busy = false;
				t->height = HEIGHT_INF;
			}
			if (m - k == 1) {
				settle(e, s);
			}
			m = k;
		}
		if (n == 0) {
			break;
		}
		struct shape *p = memo_find(&e->shapes, st[n - 1].o);
		p->low = s->low < p->low ? s->low : p->low;
	}
	free(st);
	free(scc);
	return ok;
}

static bool
fits(struct enc *e, const struct xpc_obj *v, int depth)
{
	if (container(v)) {
		struct shape *s = memo_find(&e->shapes, v);
		return s != NULL && !s->big && depth <= WIRE_DEPTH_MAX &&
		    s->height <= (uint32_t)(WIRE_DEPTH_MAX - depth);
	}
	return depth <= WIRE_DEPTH_MAX;
}

static uint32_t
tag_of(const struct xpc_obj *o)
{
	for (uint32_t tag = T_NULL; o != NULL && tag <= T_MACH_SEND; tag++) {
		if (o->type == wire_types[tag]) {
			return tag;
		}
	}
	return 0;
}

static void
encode(struct enc *e, const struct xpc_obj *o, int depth)
{
	if (e->bad || e->over) {
		return;
	}
	uint32_t tag = tag_of(o);
	if (tag == 0) {
		e->bad = true;
		return;
	}
	put32(e, tag);
	switch (tag) {
	case T_NULL:
		break;
	case T_BOOL:
		put32(e, o->b ? 1 : 0);
		break;
	case T_INT64:
	case T_UINT64:
	case T_DATE:
		put(e, &o->u, sizeof(o->u));
		break;
	case T_DOUBLE:
		put(e, &o->d, sizeof(o->d));
		break;
	case T_DATA:
	case T_STRING:
		put32(e, (uint32_t)o->buf.len);
		put(e, o->buf.ptr, o->buf.len);
		break;
	case T_UUID:
		put(e, o->uuid, sizeof(uuid_t));
		break;
	case T_ARRAY:
		put32(e, (uint32_t)o->arr.n);
		for (size_t i = 0; i < o->arr.n && !e->bad && !e->over; i++) {
			encode(e, o->arr.v[i], depth + 1);
		}
		break;
	case T_DICTIONARY: {
		uint64_t count = e->len;
		uint32_t kept = 0;
		put32(e, 0);
		for (size_t i = 0; i < o->dict.n && !e->bad && !e->over; i++) {
			if (!fits(e, o->dict.vals[i], depth + 1)) {
				continue;
			}
			size_t n = strlen(o->dict.keys[i]);
			put32(e, (uint32_t)n);
			put(e, o->dict.keys[i], n);
			encode(e, o->dict.vals[i], depth + 1);
			kept++;
		}
		if (!e->bad && !e->over) {
			memcpy(e->buf + count, &kept, sizeof(kept));
		}
		break;
	}
	case T_FD:
	case T_MACH_SEND:
		put_port(e, o->port);
		break;
	}
}

struct reach {
	const struct xpc_obj *o;
	int depth;
};

static bool
unencodable(struct enc *e, const struct xpc_obj *root)
{
	size_t head = 0, n = 0, cap = 64;
	struct reach *q = malloc(cap * sizeof(*q));
	if (q == NULL || memo_add(&e->seen, root) == NULL) {
		free(q);
		return true;
	}
	q[n++] = (struct reach){ root, 0 };
	bool bad = false;
	while (!bad && head < n) {
		struct reach r = q[head++];
		bool dict = r.o->type == XPC_TYPE_DICTIONARY;
		for (size_t i = 0; i < children(r.o) && !bad; i++) {
			const struct xpc_obj *c = child(r.o, i);
			if (dict && !fits(e, c, r.depth + 1)) {
				continue;
			}
			if (tag_of(c) == 0) {
				bad = true;
			} else if (container(c) && memo_find(&e->seen, c) == NULL) {
				void *b = grow(q, &cap, n, sizeof(*q));
				q = b != NULL ? b : q;
				if (b == NULL || memo_add(&e->seen, c) == NULL) {
					bad = true;
				} else {
					q[n++] = (struct reach){ c, r.depth + 1 };
				}
			}
		}
	}
	free(q);
	return bad;
}

static void
enc_done(struct enc *e)
{
	free(e->shapes.v);
	free(e->seen.v);
	memset(&e->shapes, 0, sizeof(e->shapes));
	memset(&e->seen, 0, sizeof(e->seen));
}

static mach_msg_ool_descriptor_t *
ool_of(mach_msg_header_t *head)
{
	if (!(head->msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
	    head->msgh_size < sizeof(*head) + sizeof(mach_msg_body_t)) {
		return NULL;
	}
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	uint8_t *p = (uint8_t *)(body + 1);
	uint8_t *end = (uint8_t *)head + head->msgh_size;
	for (mach_msg_size_t i = 0; i < body->msgh_descriptor_count; i++) {
		mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)p;
		if (p + sizeof(*td) > end) {
			return NULL;
		}
		if (td->type == MACH_MSG_OOL_DESCRIPTOR) {
			return p + sizeof(mach_msg_ool_descriptor_t) <= end ?
			    (mach_msg_ool_descriptor_t *)p : NULL;
		}
		if (td->type != MACH_MSG_PORT_DESCRIPTOR) {
			return NULL;
		}
		p += sizeof(mach_msg_port_descriptor_t);
	}
	return NULL;
}

void
_xpc_wire_destroy(mach_msg_header_t *head)
{
	mach_msg_type_name_t type = MACH_MSGH_BITS_LOCAL(head->msgh_bits);
	mach_port_t local = head->msgh_local_port;
	mach_msg_destroy(head);
	if (MACH_PORT_VALID(local) &&
	    (type == MACH_MSG_TYPE_MOVE_SEND || type == MACH_MSG_TYPE_MOVE_SEND_ONCE)) {
		mach_port_deallocate(mach_task_self(), local);
	}
}

static void
set_port(mach_msg_port_descriptor_t *pd, mach_port_t name, mach_msg_type_name_t type)
{
	pd->type = MACH_MSG_PORT_DESCRIPTOR;
	pd->name = name;
	pd->disposition = type;
}

static mach_msg_port_descriptor_t *
pds_of(mach_msg_header_t *head)
{
	return (mach_msg_port_descriptor_t *)((mach_msg_body_t *)(head + 1) + 1);
}

static unsigned
fixed_of(const struct xpc_wire_out *out)
{
	return (MACH_PORT_VALID(out->port0) ? 1u : 0u) | (MACH_PORT_VALID(out->port1) ? 2u : 0u);
}

static unsigned
fixed_count(unsigned fixed)
{
	return (unsigned)__builtin_popcount(fixed);
}

static void
set_fixed(mach_msg_header_t *head, const struct xpc_wire_out *out)
{
	mach_msg_port_descriptor_t *pd = pds_of(head);
	if (MACH_PORT_VALID(out->port0)) {
		set_port(pd++, out->port0, out->port0_type);
	}
	if (MACH_PORT_VALID(out->port1)) {
		set_port(pd, out->port1, out->port1_type);
	}
}

static kern_return_t
wire_build(xpc_object_t message, const struct xpc_wire_out *out, struct enc *e,
    mach_msg_header_t **headp)
{
	*headp = NULL;
	if (message == NULL || XO(message)->type != XPC_TYPE_DICTIONARY) {
		return KERN_INVALID_ARGUMENT;
	}
	if (!measure(e, XO(message))) {
		e->bad = true;
	}
	encode(e, XO(message), 0);
	bool bad = e->bad || (e->over && unencodable(e, XO(message)));
	enc_done(e);
	if (bad) {
		return KERN_INVALID_ARGUMENT;
	}
	if (e->over == OVER_BYTES) {
		too_large("bytes");
	}
	if (e->over == OVER_PORTS) {
		too_large("port rights");
	}
	bool ool = e->len > WIRE_INLINE_MAX;
	unsigned fixed = fixed_of(out);
	size_t npd = fixed_count(fixed) + e->nports;
	size_t ndesc = npd + (ool ? 1 : 0);
	size_t inl = ool ? 0 : round_msg(e->len);
	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    npd * sizeof(mach_msg_port_descriptor_t) + (ool ? sizeof(mach_msg_ool_descriptor_t) : 0) +
	    sizeof(struct wire_hdr) + inl;
	uint8_t *msg = calloc(1, size + sizeof(mach_msg_max_trailer_t));
	if (msg == NULL) {
		return KERN_RESOURCE_SHORTAGE;
	}

	mach_msg_header_t *head = (mach_msg_header_t *)msg;
	head->msgh_bits = MACH_MSGH_BITS_SET(out->dest_type,
	    MACH_PORT_VALID(out->reply) ? out->reply_type : 0, 0, MACH_MSGH_BITS_COMPLEX);
	head->msgh_size = (mach_msg_size_t)size;
	head->msgh_remote_port = out->dest;
	head->msgh_local_port = MACH_PORT_VALID(out->reply) ? out->reply : MACH_PORT_NULL;
	head->msgh_id = out->id;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	body->msgh_descriptor_count = (mach_msg_size_t)ndesc;

	set_fixed(head, out);
	mach_msg_port_descriptor_t *pd = pds_of(head) + fixed_count(fixed);
	for (size_t i = 0; i < e->nports; i++) {
		set_port(&pd[i], e->ports[i], MACH_MSG_TYPE_COPY_SEND);
	}
	uint8_t *p = (uint8_t *)(pd + e->nports);
	if (ool) {
		mach_msg_ool_descriptor_t *od = (mach_msg_ool_descriptor_t *)p;
		od->type = MACH_MSG_OOL_DESCRIPTOR;
		od->address = e->buf;
		od->size = (mach_msg_size_t)e->len;
		od->deallocate = false;
		od->copy = MACH_MSG_VIRTUAL_COPY;
		p += sizeof(*od);
	}
	struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, (uint32_t)e->nports, ool ? 1 : 0,
		(uint32_t)e->len, fixed };
	memcpy(p, &wh, sizeof(wh));
	p += sizeof(wh);
	if (!ool && e->len != 0) {
		memcpy(p, e->buf, e->len);
	}
	*headp = head;
	return KERN_SUCCESS;
}

static bool
header_only(kern_return_t kr)
{
	return kr == MACH_SEND_INVALID_REPLY || kr == MACH_SEND_INVALID_OPTIONS ||
	    kr == MACH_SEND_MSG_FILTERED;
}

void
_xpc_wire_destroy_body(mach_msg_header_t *head)
{
	mach_msg_bits_t bits = head->msgh_bits;
	head->msgh_bits = bits & MACH_MSGH_BITS_COMPLEX;
	mach_msg_destroy(head);
	head->msgh_bits = bits;
}

static void
drop_local(mach_msg_header_t *head)
{
	head->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSGH_BITS_REMOTE(head->msgh_bits), 0, 0,
	    head->msgh_bits & MACH_MSGH_BITS_COMPLEX);
	head->msgh_local_port = MACH_PORT_NULL;
}

static long
probe_of(mach_msg_header_t *head)
{
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	mach_msg_port_descriptor_t *pd = pds_of(head);
	for (mach_msg_size_t i = 0; i < body->msgh_descriptor_count; i++) {
		if (pd[i].type != MACH_MSG_PORT_DESCRIPTOR) {
			break;
		}
		if (pd[i].disposition == MACH_MSG_TYPE_COPY_SEND ||
		    pd[i].disposition == MACH_MSG_TYPE_MAKE_SEND) {
			return (long)i;
		}
	}
	return -1;
}

static bool
was_returned(mach_msg_header_t *sent, mach_msg_bits_t bits, long probe,
    mach_msg_type_name_t disposition, const void *ool)
{
	mach_msg_ool_descriptor_t *od = ool_of(sent);
	return sent->msgh_bits != bits || (probe >= 0 && pds_of(sent)[probe].disposition != disposition) ||
	    (od != NULL && od->address != ool);
}

static void
blank_ool(mach_msg_header_t *head)
{
	mach_msg_ool_descriptor_t *od = ool_of(head);
	if (od != NULL) {
		od->address = NULL;
		od->size = 0;
	}
}

kern_return_t
_xpc_wire_send(xpc_object_t message, const struct xpc_wire_out *out)
{
	struct enc e = { 0 };
	mach_msg_header_t *head;
	kern_return_t kr = wire_build(message, out, &e, &head);
	if (kr != KERN_SUCCESS) {
		free(e.buf);
		free(e.ports);
		return kr;
	}
	mach_msg_option_t options = MACH_SEND_MSG;
	if (!out->block && out->dest_type != MACH_MSG_TYPE_MOVE_SEND_ONCE) {
		options |= MACH_SEND_TIMEOUT;
	}
	mach_msg_bits_t bits = head->msgh_bits;
	long probe = probe_of(head);
	mach_msg_type_name_t disposition = probe >= 0 ? pds_of(head)[probe].disposition : 0;
	kr = mach_msg(head, options, head->msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
	kern_return_t base = kr & ~MACH_MSG_MASK;
	if (base == MACH_SEND_TIMED_OUT || base == MACH_SEND_NO_BUFFER) {
		if (!was_returned(head, bits, probe, disposition, e.buf)) {
			blank_ool(head);
		}
		if (MACH_MSGH_BITS_LOCAL(head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE) {
			drop_local(head);
		}
		_xpc_wire_destroy(head);
		kr = base;
	} else if (header_only(kr)) {
		blank_ool(head);
		_xpc_wire_destroy_body(head);
	}
	free(head);
	free(e.buf);
	free(e.ports);
	return kr;
}

kern_return_t
_xpc_wire_prepare(xpc_object_t message, const struct xpc_wire_out *out, struct xpc_wire_held *h)
{
	struct enc e = { 0 };
	memset(h, 0, sizeof(*h));
	h->receive = out->port0_type == MACH_MSG_TYPE_MOVE_RECEIVE && MACH_PORT_VALID(out->port0) ?
	    out->port0 : MACH_PORT_NULL;
	kern_return_t kr = wire_build(message, out, &e, &h->head);
	if (kr != KERN_SUCCESS) {
		free(e.buf);
		free(e.ports);
		return kr;
	}
	mach_msg_ool_descriptor_t *od = ool_of(h->head);
	if (od != NULL) {
		h->ool = e.buf;
	} else {
		free(e.buf);
	}
	h->ports = e.ports;
	h->nports = e.nports;
	h->fixed = fixed_of(out);
	return KERN_SUCCESS;
}

static void
relayout(struct xpc_wire_held *h, unsigned fixed)
{
	mach_msg_header_t *old = h->head;
	size_t oldn = fixed_count(h->fixed), newn = fixed_count(fixed);
	size_t size = old->msgh_size - oldn * sizeof(mach_msg_port_descriptor_t) +
	    newn * sizeof(mach_msg_port_descriptor_t);
	mach_msg_header_t *head = calloc(1, size + sizeof(mach_msg_max_trailer_t));
	if (head == NULL) {
		abort();
	}
	memcpy(head, old, sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t));
	uint8_t *rest = (uint8_t *)(pds_of(old) + oldn);
	memcpy(pds_of(head) + newn, rest, old->msgh_size - (size_t)(rest - (uint8_t *)old));
	head->msgh_size = (mach_msg_size_t)size;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	body->msgh_descriptor_count = body->msgh_descriptor_count - (mach_msg_size_t)oldn +
	    (mach_msg_size_t)newn;
	uint8_t *p = (uint8_t *)(pds_of(head) + newn + h->nports);
	if (ool_of(head) != NULL) {
		p += sizeof(mach_msg_ool_descriptor_t);
	}
	struct wire_hdr wh;
	memcpy(&wh, p, sizeof(wh));
	wh.fixed = fixed;
	memcpy(p, &wh, sizeof(wh));
	free(old);
	h->head = head;
	h->fixed = fixed;
}

void
_xpc_wire_address(struct xpc_wire_held *h, const struct xpc_wire_out *out)
{
	if (fixed_of(out) != h->fixed) {
		relayout(h, fixed_of(out));
	}
	mach_msg_header_t *head = h->head;
	head->msgh_bits = MACH_MSGH_BITS_SET(out->dest_type, MACH_MSGH_BITS_LOCAL(head->msgh_bits), 0,
	    head->msgh_bits & MACH_MSGH_BITS_COMPLEX);
	head->msgh_remote_port = out->dest;
	set_fixed(head, out);
	h->receive = out->port0_type == MACH_MSG_TYPE_MOVE_RECEIVE && MACH_PORT_VALID(out->port0) ?
	    out->port0 : MACH_PORT_NULL;
}

static void
drop_reply(struct xpc_wire_held *h)
{
	drop_local(h->head);
}

kern_return_t
_xpc_wire_try(struct xpc_wire_held *h)
{
	size_t size = h->head->msgh_size;
	mach_msg_header_t *m = malloc(size + sizeof(mach_msg_max_trailer_t));
	if (m == NULL) {
		return KERN_RESOURCE_SHORTAGE;
	}
	memcpy(m, h->head, size);
	mach_msg_port_descriptor_t *pd = pds_of(h->head);
	long probe = probe_of(h->head);
	kern_return_t kr = mach_msg(m, MACH_SEND_MSG | MACH_SEND_TIMEOUT, (mach_msg_size_t)size, 0,
	    MACH_PORT_NULL, 0, MACH_PORT_NULL);
	kern_return_t base = kr & ~MACH_MSG_MASK;
	if (base == MACH_SEND_TIMED_OUT || base == MACH_SEND_NO_BUFFER) {
		if (was_returned(m, h->head->msgh_bits, probe, probe >= 0 ? pd[probe].disposition : 0,
		    h->ool)) {
			bool wanted = MACH_MSGH_BITS_LOCAL(h->head->msgh_bits) != 0;
			mach_port_t reply = MACH_MSGH_BITS_LOCAL(m->msgh_bits) == MACH_MSG_TYPE_MOVE_SEND_ONCE ?
			    m->msgh_local_port : MACH_PORT_NULL;
			bool kept = MACH_MSGH_BITS_LOCAL(m->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE &&
			    MACH_MSGH_BITS_LOCAL(h->head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE &&
			    m->msgh_local_port == h->head->msgh_local_port;
			m->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSGH_BITS_REMOTE(m->msgh_bits), 0, 0,
			    m->msgh_bits & MACH_MSGH_BITS_COMPLEX);
			mach_msg_port_descriptor_t *back = pds_of(m);
			if ((h->fixed & 1u) && pd[0].disposition == MACH_MSG_TYPE_MOVE_RECEIVE) {
				if (back[0].name == pd[0].name &&
				    back[0].disposition == MACH_MSG_TYPE_PORT_RECEIVE) {
					back[0].name = MACH_PORT_NULL;
				} else {
					h->receive = MACH_PORT_NULL;
					kr |= MACH_MSG_IPC_SPACE;
				}
			}
			mach_msg_destroy(m);
			if (!kept && MACH_PORT_VALID(reply)) {
				h->head->msgh_bits = MACH_MSGH_BITS_SET(
				    MACH_MSGH_BITS_REMOTE(h->head->msgh_bits), MACH_MSG_TYPE_MOVE_SEND_ONCE, 0,
				    h->head->msgh_bits & MACH_MSGH_BITS_COMPLEX);
				h->head->msgh_local_port = reply;
			} else if (!kept && wanted) {
				drop_reply(h);
				kr |= MACH_MSG_IPC_SPACE;
			}
		}
		if ((kr & (MACH_MSG_IPC_SPACE | MACH_MSG_VM_SPACE)) == 0) {
			kr = base;
		}
	} else if (kr == KERN_SUCCESS || !_xpc_wire_kept(kr)) {
		drop_reply(h);
		if (!header_only(kr)) {
			h->receive = MACH_PORT_NULL;
		}
	}
	free(m);
	return kr;
}

kern_return_t
_xpc_wire_hold(struct xpc_wire_held *h)
{
	mach_port_t self = mach_task_self();
	mach_msg_port_descriptor_t *pd = pds_of(h->head) + fixed_count(h->fixed);
	h->refs = true;
	for (size_t i = 0; i < h->nports; i++) {
		if (mach_port_mod_refs(self, h->ports[i], MACH_PORT_RIGHT_SEND, 1) != KERN_SUCCESS) {
			h->ports[i] = MACH_PORT_NULL;
			pd[i].name = MACH_PORT_NULL;
		}
	}
	return KERN_SUCCESS;
}

void
_xpc_wire_release(struct xpc_wire_held *h)
{
	mach_port_t self = mach_task_self();
	if (MACH_PORT_VALID(h->receive)) {
		mach_port_mod_refs(self, h->receive, MACH_PORT_RIGHT_RECEIVE, -1);
		h->receive = MACH_PORT_NULL;
	}
	if (h->head == NULL) {
		return;
	}
	if (h->refs) {
		for (size_t i = 0; i < h->nports; i++) {
			if (MACH_PORT_VALID(h->ports[i])) {
				mach_port_deallocate(self, h->ports[i]);
			}
		}
	}
	mach_msg_type_name_t local = MACH_MSGH_BITS_LOCAL(h->head->msgh_bits);
	if (local == MACH_MSG_TYPE_MOVE_SEND_ONCE && MACH_PORT_VALID(h->head->msgh_local_port)) {
		mach_port_deallocate(self, h->head->msgh_local_port);
	} else if (local == MACH_MSG_TYPE_MAKE_SEND_ONCE && h->refs &&
	    MACH_PORT_VALID(h->head->msgh_local_port)) {
		mach_msg_header_t m = {
			.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_MAKE_SEND_ONCE, 0, 0, 0),
			.msgh_size = sizeof(m),
			.msgh_remote_port = h->head->msgh_local_port,
			.msgh_id = XPC_WIRE_WAKE,
		};
		mach_msg(&m, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(m), 0, MACH_PORT_NULL, 0,
		    MACH_PORT_NULL);
	}
	free(h->head);
	free(h->ool);
	free(h->ports);
	memset(h, 0, sizeof(*h));
}

bool
_xpc_wire_kept(kern_return_t kr)
{
	switch (kr) {
	case KERN_INVALID_ARGUMENT:
	case KERN_RESOURCE_SHORTAGE:
	case MACH_SEND_MSG_TOO_SMALL:
	case MACH_SEND_TOO_LARGE:
	case MACH_SEND_INVALID_TYPE:
	case MACH_SEND_INVALID_DATA:
	case MACH_SEND_INVALID_HEADER:
	case MACH_SEND_INVALID_DEST:
	case MACH_SEND_INVALID_VOUCHER:
		return true;
	default:
		return false;
	}
}

kern_return_t
_xpc_wire_receive(mach_port_t port, mach_msg_option_t options, mach_msg_timeout_t timeout,
    mach_msg_header_t **message)
{
	mach_msg_size_t size = WIRE_RCV_SIZE;
	*message = NULL;
	mach_msg_header_t *head = malloc(size);
	if (head == NULL) {
		return KERN_RESOURCE_SHORTAGE;
	}
	for (;;) {
		kern_return_t kr = mach_msg(head, MACH_RCV_MSG | MACH_RCV_LARGE | WIRE_TRAILER | options,
		    0, size, port, timeout, MACH_PORT_NULL);
		if (kr == MACH_RCV_TOO_LARGE) {
			mach_msg_size_t need = head->msgh_size;
			free(head);
			head = NULL;
			if (need <= UINT32_MAX - sizeof(mach_msg_max_trailer_t)) {
				size = need + (mach_msg_size_t)sizeof(mach_msg_max_trailer_t);
				head = malloc(size);
			}
			if (head == NULL) {
				mach_msg_header_t small[2];
				return mach_msg(small, MACH_RCV_MSG | WIRE_TRAILER | MACH_RCV_TIMEOUT, 0,
				    sizeof(small), port, 0, MACH_PORT_NULL);
			}
			continue;
		}
		if ((kr & ~MACH_MSG_MASK) == MACH_RCV_BODY_ERROR) {
			mach_msg_destroy(head);
			kr = MACH_RCV_BODY_ERROR;
		}
		if (kr != KERN_SUCCESS) {
			free(head);
			return kr;
		}
		*message = head;
		return KERN_SUCCESS;
	}
}

const audit_token_t *
_xpc_wire_audit_token(mach_msg_header_t *head)
{
	mach_msg_audit_trailer_t *t =
	    (mach_msg_audit_trailer_t *)((uint8_t *)head + round_msg(head->msgh_size));
	if (t->msgh_trailer_size < sizeof(mach_msg_audit_trailer_t)) {
		return NULL;
	}
	return &t->msgh_audit;
}

struct dec {
	const uint8_t *p;
	size_t left;
	mach_port_t *ports;
	bool *used;
	uint32_t nports;
};

static bool
get(struct dec *d, void *out, size_t n)
{
	if (d->left < n) {
		return false;
	}
	memcpy(out, d->p, n);
	d->p += n;
	d->left -= n;
	return true;
}

static bool
get32(struct dec *d, uint32_t *v)
{
	return get(d, v, sizeof(*v));
}

static xpc_object_t
decode(struct dec *d, int depth)
{
	uint32_t tag, n;
	uint64_t u;
	if (depth > WIRE_DEPTH_MAX || !get32(d, &tag)) {
		return NULL;
	}
	switch (tag) {
	case T_NULL:
		return xpc_null_create();
	case T_BOOL:
		return get32(d, &n) ? xpc_bool_create(n != 0) : NULL;
	case T_INT64:
	case T_UINT64:
	case T_DATE:
	case T_DOUBLE: {
		if (!get(d, &u, sizeof(u))) {
			return NULL;
		}
		xpc_type_t t = tag == T_INT64 ? XPC_TYPE_INT64 : tag == T_UINT64 ? XPC_TYPE_UINT64 :
		    tag == T_DATE ? XPC_TYPE_DATE : XPC_TYPE_DOUBLE;
		struct xpc_obj *o = _xpc_alloc(t);
		o->u = u;
		return o;
	}
	case T_DATA:
	case T_STRING:
		if (!get32(d, &n) || d->left < n) {
			return NULL;
		}
		if (tag == T_STRING && memchr(d->p, '\0', n) != NULL) {
			return NULL;
		} else {
			xpc_object_t o = _xpc_buf_create(tag == T_DATA ? XPC_TYPE_DATA : XPC_TYPE_STRING,
			    d->p, n);
			d->p += n;
			d->left -= n;
			return o;
		}
	case T_UUID: {
		uuid_t uu;
		return get(d, uu, sizeof(uu)) ? xpc_uuid_create(uu) : NULL;
	}
	case T_ARRAY: {
		if (!get32(d, &n) || n >= wire_array_max || n > d->left / sizeof(uint32_t)) {
			return NULL;
		}
		xpc_object_t a = xpc_array_create(NULL, 0);
		for (uint32_t i = 0; i < n; i++) {
			xpc_object_t v = decode(d, depth + 1);
			if (v == NULL) {
				xpc_release(a);
				return NULL;
			}
			xpc_array_append_value(a, v);
			xpc_release(v);
		}
		return a;
	}
	case T_DICTIONARY: {
		if (!get32(d, &n)) {
			return NULL;
		}
		xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t klen;
			if (!get32(d, &klen) || d->left < klen || memchr(d->p, '\0', klen) != NULL) {
				xpc_release(dict);
				return NULL;
			}
			char *key = malloc((size_t)klen + 1);
			if (key == NULL) {
				abort();
			}
			memcpy(key, d->p, klen);
			key[klen] = '\0';
			d->p += klen;
			d->left -= klen;
			xpc_object_t v = decode(d, depth + 1);
			if (v == NULL) {
				free(key);
				xpc_release(dict);
				return NULL;
			}
			_xpc_dictionary_append(dict, key, v);
		}
		if (!_xpc_dictionary_keys_unique(dict)) {
			xpc_release(dict);
			return NULL;
		}
		return dict;
	}
	case T_FD:
	case T_MACH_SEND:
		if (!get32(d, &n) || n >= d->nports || d->used[n] || !MACH_PORT_VALID(d->ports[n])) {
			return NULL;
		}
		d->used[n] = true;
		return _xpc_port_create(tag == T_FD ? XPC_TYPE_FD : XPC_TYPE_MACH_SEND, d->ports[n]);
	default:
		return NULL;
	}
}

static bool
port_is(mach_msg_port_descriptor_t *pd, mach_msg_type_name_t want)
{
	return !MACH_PORT_VALID(pd->name) || pd->disposition == want;
}

static mach_port_t
port_of(mach_msg_port_descriptor_t *pd)
{
	return MACH_PORT_VALID(pd->name) ? pd->name : MACH_PORT_NULL;
}

int
_xpc_wire_decode(mach_msg_header_t *head, xpc_object_t *message, mach_port_t *port0,
    mach_port_t *port1)
{
	*message = NULL;
	*port0 = *port1 = MACH_PORT_NULL;
	bool connect = head->msgh_id == XPC_WIRE_CONNECT;
	if (head->msgh_id != XPC_WIRE_MESSAGE && head->msgh_id != XPC_WIRE_REQUEST &&
	    head->msgh_id != XPC_WIRE_REPLY && !connect) {
		return XPC_WIRE_NOT_XPC;
	}
	if (!(head->msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
	    head->msgh_size < sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t)) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}
	uint8_t *end = (uint8_t *)head + head->msgh_size;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	mach_msg_size_t ndesc = body->msgh_descriptor_count;
	mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)(body + 1);
	if ((size_t)(end - (uint8_t *)pd) / sizeof(*pd) < ndesc) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}

	uint32_t npd = 0;
	mach_msg_ool_descriptor_t *od = NULL;
	uint8_t *p = (uint8_t *)pd;
	for (mach_msg_size_t i = 0; i < ndesc; i++) {
		mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)p;
		if (td->type == MACH_MSG_PORT_DESCRIPTOR) {
			npd++;
			p += sizeof(mach_msg_port_descriptor_t);
		} else if (td->type == MACH_MSG_OOL_DESCRIPTOR && i == ndesc - 1) {
			od = (mach_msg_ool_descriptor_t *)p;
			p += sizeof(mach_msg_ool_descriptor_t);
		} else {
			mach_msg_destroy(head);
			return XPC_WIRE_BAD;
		}
	}
	struct wire_hdr wh;
	if (p > end || (size_t)(end - p) < sizeof(wh)) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}
	memcpy(&wh, p, sizeof(wh));
	p += sizeof(wh);
	const uint8_t *payload = od != NULL ? od->address : p;
	uint64_t inline_size = (uint64_t)(end - p);
	uint64_t want_inline = od != NULL ? 0 : ((uint64_t)wh.length + 3) & ~(uint64_t)3;
	if (wh.magic != WIRE_MAGIC || wh.version != WIRE_VERSION || (wh.fixed & ~3u) != 0 ||
	    wh.nports > XPC_WIRE_PORTS_MAX || wh.ool != (od != NULL) || inline_size != want_inline ||
	    (od != NULL && od->size != wh.length)) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}
	if ((uint64_t)npd != (uint64_t)fixed_count(wh.fixed) + wh.nports || npd > XPC_WIRE_PORTS_MAX ||
	    (connect && (wh.fixed != 3u || wh.nports != 0))) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}
	uint32_t nports = wh.nports;
	mach_msg_port_descriptor_t *pd0 = (wh.fixed & 1u) ? pd : NULL;
	mach_msg_port_descriptor_t *pd1 = (wh.fixed & 2u) ? pd + (wh.fixed & 1u) : NULL;
	mach_msg_port_descriptor_t *upd = pd + fixed_count(wh.fixed);

	mach_msg_type_name_t rtype = MACH_MSGH_BITS_REMOTE(head->msgh_bits);
	mach_port_t reply = MACH_PORT_VALID(head->msgh_remote_port) ? head->msgh_remote_port :
	    MACH_PORT_NULL;
	bool ok = (pd0 == NULL || port_is(pd0, MACH_MSG_TYPE_PORT_RECEIVE)) &&
	    (pd1 == NULL || port_is(pd1, MACH_MSG_TYPE_PORT_SEND)) &&
	    (head->msgh_id != XPC_WIRE_REQUEST || rtype == MACH_MSG_TYPE_PORT_SEND_ONCE);
	for (uint32_t i = 0; i < nports && ok; i++) {
		ok = port_is(&upd[i], MACH_MSG_TYPE_PORT_SEND);
	}
	if (!ok) {
		mach_msg_destroy(head);
		return XPC_WIRE_BAD;
	}
	mach_port_t p0 = pd0 != NULL ? port_of(pd0) : MACH_PORT_NULL;
	mach_port_t p1 = pd1 != NULL ? port_of(pd1) : MACH_PORT_NULL;
	mach_port_t *ports = malloc((nports ? nports : 1) * (sizeof(*ports) + sizeof(bool)));
	if (ports == NULL) {
		abort();
	}
	bool *used = (bool *)(ports + nports);
	for (uint32_t i = 0; i < nports; i++) {
		ports[i] = port_of(&upd[i]);
		used[i] = false;
	}

	struct dec d = { payload, wh.length, ports, used, nports };
	xpc_object_t dict = decode(&d, 0);
	if (dict != NULL && (XO(dict)->type != XPC_TYPE_DICTIONARY || d.left != 0 ||
	    (connect && xpc_dictionary_get_count(dict) != 0))) {
		xpc_release(dict);
		dict = NULL;
	}
	for (uint32_t i = 0; i < nports; i++) {
		if (!used[i]) {
			_xpc_port_release(ports[i], MACH_MSG_TYPE_PORT_SEND);
		}
	}
	free(ports);
	if (od != NULL) {
		vm_deallocate(mach_task_self(), (vm_address_t)od->address, od->size);
	}
	if (dict == NULL) {
		_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
		_xpc_port_release(reply, rtype);
		return XPC_WIRE_BAD;
	}

	struct xpc_msginfo *m = _xpc_dictionary_msginfo(dict, true);
	const audit_token_t *tok = _xpc_wire_audit_token(head);
	if (tok != NULL) {
		m->token = *tok;
		m->has_token = true;
	}
	if (head->msgh_id == XPC_WIRE_REQUEST ||
	    (head->msgh_id == XPC_WIRE_REPLY && rtype == MACH_MSG_TYPE_PORT_SEND_ONCE)) {
		m->reply = reply;
	} else {
		_xpc_port_release(reply, rtype);
	}
	*port0 = p0;
	*port1 = p1;
	*message = dict;
	return XPC_WIRE_OK;
}
