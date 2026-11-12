/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "../wire.c"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__aarch64__) || defined(__arm64__)
#define TRAP_SIGNAL SIGTRAP
#else
#define TRAP_SIGNAL SIGILL
#endif

#define QUEUE_MAX 8

struct queued {
	mach_msg_header_t *msg;
	audit_token_t token;
};

static struct {
	kern_return_t send_result;
	mach_msg_option_t send_options;
	mach_msg_header_t *sent;
	int sends;
	int destroys;
	int deallocs;
	int vm_deallocs;
	int addrefs;
	int receive_drops;
	mach_port_t receive_dropped;
	bool fail_addref;
	bool pseudo;
	bool lose_reply;
	bool rename_receive;
	mach_msg_bits_t destroy_bits;
	mach_port_t destroy_port0;
	mach_port_t dealloc_names[16];
	struct queued queue[QUEUE_MAX];
	int queued;
	int receives;
	mach_msg_size_t receive_sizes[8];
	bool body_error;
	bool reply_port;
	bool rename_reply;
	size_t fail_malloc;
	size_t fail_calloc;
	int fail_calloc_skip;
	size_t fail_realloc_size;
	int alloc_failed;
	void *realloc_kept;
	bool realloc_kept_freed;
} host;

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ASAN 1
#endif
#endif

#ifndef TEST_ASAN
extern void *__libc_malloc(size_t size);

extern void *__libc_calloc(size_t n, size_t size);
extern void *__libc_realloc(void *p, size_t size);
extern void __libc_free(void *p);

void
free(void *p)
{
	if (p != NULL && p == host.realloc_kept) {
		host.realloc_kept = NULL;
		host.realloc_kept_freed = true;
	}
	__libc_free(p);
}

void *
malloc(size_t size)
{
	if (host.fail_malloc != 0 && size >= host.fail_malloc) {
		host.fail_malloc = 0;
		host.alloc_failed++;
		return NULL;
	}
	return __libc_malloc(size);
}

void *
calloc(size_t n, size_t size)
{
	if (host.fail_calloc != 0 && n * size >= host.fail_calloc) {
		if (host.fail_calloc_skip > 0) {
			host.fail_calloc_skip--;
		} else {
			host.fail_calloc = 0;
			host.alloc_failed++;
			return NULL;
		}
	}
	return __libc_calloc(n, size);
}

void *
realloc(void *p, size_t size)
{
	if (host.fail_realloc_size != 0 && size == host.fail_realloc_size) {
		host.fail_realloc_size = 0;
		host.alloc_failed++;
		host.realloc_kept = p;
		host.realloc_kept_freed = false;
		return NULL;
	}
	return __libc_realloc(p, size);
}
#endif

static int failures;
static int faults;

void
test_os_log(void *log, const char *format, ...)
{
	(void)log;
	(void)format;
	faults++;
}

static xpc_object_t trap_message;
static struct xpc_wire_out trap_out;

static int
send_status(void)
{
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		struct rlimit none = { 0, 0 };
		setrlimit(RLIMIT_CORE, &none);
#ifdef __linux__
		prctl(PR_SET_DUMPABLE, 0);
#endif
		kern_return_t kr = _xpc_wire_send(trap_message, &trap_out);
		_exit(kr == KERN_SUCCESS ? 0 : 1);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return status;
}

static bool
send_traps(void)
{
	int status = send_status();
	return WIFSIGNALED(status) && WTERMSIG(status) == TRAP_SIGNAL;
}

static bool
send_works(void)
{
	int status = send_status();
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool
send_refused(void)
{
	int status = send_status();
	return WIFEXITED(status) && WEXITSTATUS(status) == 1;
}

static double
now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

const struct _xpc_type_s _xpc_type_connection = { "connection", NULL };
const struct _xpc_type_s _xpc_type_endpoint = { "endpoint", NULL };

mach_port_t
mach_task_self(void)
{
	return 1;
}

kern_return_t
mach_port_deallocate(mach_port_t task, mach_port_name_t name)
{
	(void)task;
	if (host.deallocs < 16) {
		host.dealloc_names[host.deallocs] = name;
	}
	host.deallocs++;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right,
    mach_port_delta_t delta)
{
	(void)task;
	if (right == MACH_PORT_RIGHT_RECEIVE) {
		host.receive_drops++;
		host.receive_dropped = name;
	} else if (delta < 0) {
		host.deallocs++;
	} else if (host.fail_addref) {
		return KERN_INVALID_RIGHT;
	} else {
		host.addrefs++;
	}
	return KERN_SUCCESS;
}

kern_return_t
mach_port_extract_right(mach_port_t task, mach_port_name_t name, mach_msg_type_name_t desired,
    mach_port_t *right, mach_msg_type_name_t *acquired)
{
	(void)task;
	(void)name;
	*right = 900;
	*acquired = desired == MACH_MSG_TYPE_MAKE_SEND_ONCE ? MACH_MSG_TYPE_MOVE_SEND_ONCE : desired;
	return KERN_SUCCESS;
}

kern_return_t
vm_deallocate(mach_port_t task, vm_address_t address, vm_size_t size)
{
	(void)task;
	(void)size;
	free((void *)address);
	host.vm_deallocs++;
	return KERN_SUCCESS;
}

int
fileport_makeport(int fd, mach_port_t *port)
{
	(void)fd;
	*port = MACH_PORT_NULL;
	return -1;
}

int
fileport_makefd(mach_port_t port)
{
	(void)port;
	return -1;
}

void
uuid_unparse_upper(const uuid_t uu, char *out)
{
	for (int i = 0; i < 16; i++) {
		sprintf(out + 2 * i, "%02X", uu[i]);
	}
}

int
heapsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *))
{
	qsort(base, nel, width, compar);
	return 0;
}

static mach_msg_header_t *
copy_message(const mach_msg_header_t *msg)
{
	mach_msg_header_t *m = calloc(1, round_msg(msg->msgh_size) + sizeof(mach_msg_audit_trailer_t));
	memcpy(m, msg, msg->msgh_size);
	mach_msg_ool_descriptor_t *od = ool_of(m);
	if (od != NULL) {
		void *data = malloc(od->size ? od->size : 1);
		memcpy(data, od->address, od->size);
		od->address = data;
	}
	return m;
}

void
mach_msg_destroy(mach_msg_header_t *msg)
{
	mach_msg_ool_descriptor_t *od = ool_of(msg);
	if (od != NULL && od->size != 0) {
		vm_deallocate(mach_task_self(), (vm_address_t)od->address, od->size);
	}
	host.destroy_bits = msg->msgh_bits;
	if ((msg->msgh_bits & MACH_MSGH_BITS_COMPLEX) && msg->msgh_size >= sizeof(*msg) +
	    sizeof(mach_msg_body_t) + sizeof(mach_msg_port_descriptor_t)) {
		host.destroy_port0 = ((mach_msg_port_descriptor_t *)((mach_msg_body_t *)(msg + 1) + 1))[0].name;
	}
	host.destroys++;
}

static mach_msg_type_name_t
received(mach_msg_type_name_t type)
{
	switch (type) {
	case MACH_MSG_TYPE_COPY_SEND:
	case MACH_MSG_TYPE_MAKE_SEND:
		return MACH_MSG_TYPE_MOVE_SEND;
	case MACH_MSG_TYPE_MAKE_SEND_ONCE:
		return MACH_MSG_TYPE_MOVE_SEND_ONCE;
	default:
		return type;
	}
}

static void
pseudo_receive(mach_msg_header_t *msg)
{
	mach_msg_type_name_t local = MACH_MSGH_BITS_LOCAL(msg->msgh_bits);
	if (!(host.reply_port && local == MACH_MSG_TYPE_MAKE_SEND_ONCE)) {
		local = received(local);
	} else if (host.rename_reply) {
		msg->msgh_local_port = 78;
	}
	msg->msgh_bits = MACH_MSGH_BITS_SET(received(MACH_MSGH_BITS_REMOTE(msg->msgh_bits)), local, 0,
	    msg->msgh_bits & MACH_MSGH_BITS_COMPLEX);
	mach_msg_body_t *body = (mach_msg_body_t *)(msg + 1);
	uint8_t *p = (uint8_t *)(body + 1);
	for (mach_msg_size_t i = 0; i < body->msgh_descriptor_count; i++) {
		mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)p;
		if (td->type == MACH_MSG_PORT_DESCRIPTOR) {
			mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)p;
			pd->disposition = received(pd->disposition);
			if (host.rename_receive && pd->disposition == MACH_MSG_TYPE_PORT_RECEIVE) {
				pd->name = 77;
			}
			p += sizeof(*pd);
		} else {
			mach_msg_ool_descriptor_t *od = (mach_msg_ool_descriptor_t *)p;
			void *copy = malloc(od->size ? od->size : 1);
			memcpy(copy, od->address, od->size);
			if (od->deallocate) {
				free(od->address);
			}
			od->address = copy;
			p += sizeof(*od);
		}
	}
}

static void
discard(mach_msg_header_t *msg)
{
	mach_msg_ool_descriptor_t *od = ool_of(msg);
	if (od != NULL) {
		free(od->address);
	}
	free(msg);
}

mach_msg_return_t
mach_msg(mach_msg_header_t *msg, mach_msg_option_t option, mach_msg_size_t send_size,
    mach_msg_size_t rcv_size, mach_port_name_t rcv_name, mach_msg_timeout_t timeout,
    mach_port_name_t notify)
{
	(void)send_size;
	(void)rcv_name;
	(void)timeout;
	(void)notify;
	if (option & MACH_SEND_MSG) {
		host.sends++;
		host.send_options = option;
		if (host.send_result != KERN_SUCCESS) {
			if (host.pseudo) {
				pseudo_receive(msg);
				if (host.lose_reply) {
					msg->msgh_local_port = MACH_PORT_NULL;
					msg->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSGH_BITS_REMOTE(msg->msgh_bits), 0,
					    0, msg->msgh_bits & MACH_MSGH_BITS_COMPLEX);
					return host.send_result | MACH_MSG_IPC_SPACE;
				}
			}
			return host.send_result;
		}
		if (host.sent != NULL) {
			discard(host.sent);
		}
		host.sent = copy_message(msg);
		mach_msg_ool_descriptor_t *od = ool_of(msg);
		if (od != NULL && od->deallocate) {
			free(od->address);
		}
		return KERN_SUCCESS;
	}
	if (host.receives < 8) {
		host.receive_sizes[host.receives] = rcv_size;
	}
	host.receives++;
	if (host.queued == 0) {
		return MACH_RCV_TIMED_OUT;
	}
	struct queued *q = &host.queue[0];
	mach_msg_size_t need = round_msg(q->msg->msgh_size) + sizeof(mach_msg_audit_trailer_t);
	if (need > rcv_size) {
		msg->msgh_size = q->msg->msgh_size;
		if (option & MACH_RCV_LARGE) {
			return MACH_RCV_TOO_LARGE;
		}
	} else {
		memcpy(msg, q->msg, q->msg->msgh_size);
		mach_msg_audit_trailer_t *t =
		    (mach_msg_audit_trailer_t *)((uint8_t *)msg + round_msg(msg->msgh_size));
		memset(t, 0, sizeof(*t));
		t->msgh_trailer_size = sizeof(*t);
		t->msgh_audit = q->token;
	}
	if (need > rcv_size) {
		discard(q->msg);
	} else {
		free(q->msg);
	}
	host.queued--;
	memmove(&host.queue[0], &host.queue[1], (size_t)host.queued * sizeof(host.queue[0]));
	if (need <= rcv_size && host.body_error) {
		return MACH_RCV_BODY_ERROR | MACH_MSG_VM_SPACE;
	}
	return need > rcv_size ? MACH_RCV_TOO_LARGE : KERN_SUCCESS;
}

static void
reset(void)
{
	if (host.sent != NULL) {
		discard(host.sent);
	}
	while (host.queued > 0) {
		discard(host.queue[--host.queued].msg);
	}
	memset(&host, 0, sizeof(host));
}

static void
enqueue(mach_msg_header_t *msg, pid_t pid)
{
	struct queued *q = &host.queue[host.queued++];
	q->msg = msg;
	memset(&q->token, 0, sizeof(q->token));
	q->token.val[5] = (unsigned int)pid;
}

static struct xpc_wire_out
message_to(mach_port_t dest, mach_msg_type_name_t type)
{
	struct xpc_wire_out out = {
		.dest = dest,
		.dest_type = type,
		.id = XPC_WIRE_MESSAGE,
	};
	return out;
}

static void
test_send_timeout(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(m, "k", "v");

	reset();
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
	CHECK(host.send_options & MACH_SEND_TIMEOUT);

	reset();
	out = message_to(5, MACH_MSG_TYPE_MOVE_SEND_ONCE);
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
	CHECK(!(host.send_options & MACH_SEND_TIMEOUT));

	reset();
	out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.block = true;
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
	CHECK(!(host.send_options & MACH_SEND_TIMEOUT));

	reset();
	host.send_result = MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE;
	host.pseudo = true;
	out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_TIMED_OUT);
	CHECK(host.destroys == 1);

	xpc_release(m);
}

static mach_msg_header_t *
craft(const void *payload, size_t len, bool ool, size_t ool_size)
{
	size_t inl = ool ? 0 : round_msg(len);
	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    (ool ? sizeof(mach_msg_ool_descriptor_t) : 0) + sizeof(struct wire_hdr) + inl;
	uint8_t *m = calloc(1, size + sizeof(mach_msg_audit_trailer_t));
	mach_msg_header_t *head = (mach_msg_header_t *)m;
	head->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	head->msgh_size = (mach_msg_size_t)size;
	head->msgh_id = XPC_WIRE_MESSAGE;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	body->msgh_descriptor_count = ool ? 1 : 0;
	uint8_t *p = (uint8_t *)(body + 1);
	if (ool) {
		mach_msg_ool_descriptor_t *od = (mach_msg_ool_descriptor_t *)p;
		od->type = MACH_MSG_OOL_DESCRIPTOR;
		od->address = calloc(1, ool_size ? ool_size : 1);
		if (len != 0) {
			memcpy(od->address, payload, len);
		}
		od->size = (mach_msg_size_t)ool_size;
		p += sizeof(*od);
	}
	struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, 0, ool ? 1 : 0, (uint32_t)len, 0 };
	memcpy(p, &wh, sizeof(wh));
	if (!ool && len != 0) {
		memcpy(p + sizeof(wh), payload, len);
	}
	return head;
}

static int
decode_payload(struct enc *e, bool ool, size_t ool_size, xpc_object_t *out)
{
	mach_msg_header_t *head = craft(e->buf, e->len, ool, ool_size);
	mach_port_t p0, p1;
	int r = _xpc_wire_decode(head, out, &p0, &p1);
	if (r == XPC_WIRE_OK) {
		_xpc_port_release(p0, MACH_MSG_TYPE_PORT_RECEIVE);
		_xpc_port_release(p1, MACH_MSG_TYPE_PORT_SEND);
	}
	free(head);
	free(e->buf);
	free(e->ports);
	memset(e, 0, sizeof(*e));
	return r;
}

static void
put_key(struct enc *e, const char *key)
{
	put32(e, (uint32_t)strlen(key));
	put(e, key, strlen(key));
}

static void
test_round_trip(void)
{
	static const uuid_t uu = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(m, "string", "value");
	xpc_dictionary_set_int64(m, "int64", -5);
	xpc_dictionary_set_uint64(m, "uint64", 5);
	xpc_dictionary_set_bool(m, "bool", true);
	xpc_dictionary_set_double(m, "double", 0.5);
	xpc_dictionary_set_uuid(m, "uuid", uu);
	xpc_dictionary_set_data(m, "data", "\0\1\2", 3);
	xpc_object_t inner = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(inner, "inner", "value");
	xpc_object_t a = xpc_array_create(NULL, 0);
	xpc_array_append_value(a, inner);
	xpc_array_append_value(a, xpc_null_create());
	xpc_dictionary_set_value(m, "array", a);
	xpc_release(a);
	xpc_release(inner);

	for (int big = 0; big < 2; big++) {
		reset();
		if (big) {
			size_t n = 100000;
			char *data = calloc(1, n);
			xpc_dictionary_set_data(m, "big", data, n);
			free(data);
		}
		struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
		CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
		CHECK((ool_of(host.sent) != NULL) == big);
		mach_msg_header_t *head = copy_message(host.sent);
		xpc_object_t got = NULL;
		mach_port_t p0, p1;
		CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK);
		CHECK(got != NULL && xpc_equal(m, got));
		CHECK(host.vm_deallocs == big);
		xpc_release(got);
		free(head);
	}
	xpc_release(m);
}

static void
test_duplicate_keys(void)
{
	struct enc e = { 0 };
	xpc_object_t got = NULL;
	put32(&e, T_DICTIONARY);
	put32(&e, 3);
	put_key(&e, "a");
	put32(&e, T_NULL);
	put_key(&e, "b");
	put32(&e, T_NULL);
	put_key(&e, "a");
	put32(&e, T_NULL);
	CHECK(decode_payload(&e, false, 0, &got) == XPC_WIRE_BAD);

	put32(&e, T_DICTIONARY);
	put32(&e, 3);
	put_key(&e, "a");
	put32(&e, T_NULL);
	put_key(&e, "b");
	put32(&e, T_NULL);
	put_key(&e, "c");
	put32(&e, T_NULL);
	CHECK(decode_payload(&e, false, 0, &got) == XPC_WIRE_OK);
	CHECK(xpc_dictionary_get_count(got) == 3);
	xpc_release(got);
}

static void
test_many_keys(void)
{
	const uint32_t n = 200000;
	struct enc e = { 0 };
	xpc_object_t got = NULL;
	char key[16];
	put32(&e, T_DICTIONARY);
	put32(&e, n);
	for (uint32_t i = 0; i < n; i++) {
		snprintf(key, sizeof(key), "k%07u", i);
		put_key(&e, key);
		put32(&e, T_NULL);
	}
	size_t len = e.len;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	CHECK(decode_payload(&e, true, len, &got) == XPC_WIRE_OK);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
	CHECK(xpc_dictionary_get_count(got) == n);
	CHECK(secs < 5.0);
	printf("wire_test: %u keys decoded in %.3f s\n", n, secs);
	xpc_release(got);
}

static void
test_sizes(void)
{
	struct enc e = { 0 };
	xpc_object_t got = NULL;
	const size_t big = (9u << 20) + 3;

	put32(&e, T_DICTIONARY);
	put32(&e, 0);
	CHECK(decode_payload(&e, true, big, &got) == XPC_WIRE_BAD && got == NULL);
	put32(&e, T_DICTIONARY);
	put32(&e, 0);
	CHECK(decode_payload(&e, true, 8, &got) == XPC_WIRE_OK);
	xpc_release(got);

	put32(&e, T_DICTIONARY);
	put32(&e, 1);
	put_key(&e, "d");
	put32(&e, T_DATA);
	put32(&e, WIRE_INLINE_MAX);
	char *data = calloc(1, big);
	put(&e, data, WIRE_INLINE_MAX);
	CHECK(decode_payload(&e, false, 0, &got) == XPC_WIRE_OK);
	CHECK(xpc_dictionary_get_count(got) == 1);
	xpc_release(got);

	reset();
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	data[big - 1] = 7;
	xpc_dictionary_set_data(m, "d", data, big);
	free(data);
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
	CHECK(host.sends == 1 && ool_of(host.sent) != NULL && ool_of(host.sent)->size > big);
	mach_msg_header_t *head = copy_message(host.sent);
	mach_port_t p0, p1;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && xpc_equal(m, got));
	xpc_release(got);
	free(head);
	xpc_release(m);

	const unsigned nports = 16381;
	reset();
	m = xpc_dictionary_create(NULL, NULL, 0);
	for (unsigned int i = 0; i < nports; i++) {
		char key[16];
		snprintf(key, sizeof(key), "p%u", i);
		xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 100 + i);
		xpc_dictionary_set_value(m, key, port);
		xpc_release(port);
	}
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS);
	CHECK(host.sends == 1 && host.sent->msgh_size > 65536);
	CHECK(((mach_msg_body_t *)(host.sent + 1))->msgh_descriptor_count == 1 + nports);
	head = copy_message(host.sent);
	pseudo_receive(head);
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && xpc_dictionary_get_count(got) == nports);
	CHECK(XO(xpc_dictionary_get_value(got, "p16380"))->port == 100 + 16380);
	xpc_release(got);
	free(head);
	xpc_release(m);
}

static xpc_object_t
ports_dict(unsigned n)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t a = xpc_array_create(NULL, 0);
	for (unsigned i = 0; i < n; i++) {
		xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 100 + i);
		xpc_array_append_value(a, port);
		XO(port)->port = MACH_PORT_NULL;
		xpc_release(port);
	}
	xpc_dictionary_set_value(m, "ports", a);
	xpc_release(a);
	return m;
}

static void
test_oversize_traps(void)
{
	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	CHECK(XPC_WIRE_PORTS_MAX == 16383 && wire_len_max == UINT32_MAX);
	trap_message = ports_dict(16383);
	CHECK(send_works());
	xpc_release(trap_message);
	trap_message = ports_dict(16384);
	CHECK(send_traps());
	trap_out.reply = 9;
	trap_out.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
	CHECK(send_traps());
	trap_out = message_to(7, MACH_MSG_TYPE_MOVE_SEND_ONCE);
	trap_out.id = XPC_WIRE_REPLY;
	CHECK(send_traps());
	xpc_release(trap_message);

	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	wire_len_max = 100000;
	char *data = calloc(1, 100000);
	for (size_t n = 100000 - 21; n <= 100000 - 20; n++) {
		trap_message = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_data(trap_message, "d", data, n);
		CHECK(n == 100000 - 21 ? send_works() : send_traps());
		xpc_release(trap_message);
	}
	wire_len_max = UINT32_MAX;
	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_data(trap_message, "d", data, 100000 - 21);
	struct enc e = { 0 };
	encode(&e, XO(trap_message), 0);
	CHECK(e.len == 100000);
	free(e.buf);
	free(e.ports);
	xpc_release(trap_message);
	wire_len_max = 100000;
	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t big = _xpc_buf_create(XPC_TYPE_STRING, data, 10);
	XO(big)->buf.len = 100001;
	xpc_dictionary_set_value(trap_message, "s", big);
	CHECK(send_traps());
	XO(big)->buf.len = 10;
	xpc_release(big);
	xpc_release(trap_message);
	free(data);
	wire_len_max = UINT32_MAX;
	reset();
}

static xpc_object_t
nest(unsigned n)
{
	xpc_object_t a = xpc_array_create(NULL, 0);
	for (unsigned i = 1; i < n; i++) {
		xpc_object_t b = xpc_array_create(&a, 1);
		xpc_release(a);
		a = b;
	}
	return a;
}

static unsigned
depth_of(xpc_object_t v)
{
	unsigned d = 0;
	while (v != NULL && xpc_get_type(v) == XPC_TYPE_ARRAY) {
		d++;
		v = xpc_array_get_count(v) > 0 ? xpc_array_get_value(v, 0) : NULL;
	}
	return d;
}

static xpc_object_t
round_trip(xpc_object_t m)
{
	reset();
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	if (_xpc_wire_send(m, &out) != KERN_SUCCESS) {
		return NULL;
	}
	mach_msg_header_t *head = copy_message(host.sent);
	pseudo_receive(head);
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	if (_xpc_wire_decode(head, &got, &p0, &p1) != XPC_WIRE_OK) {
		got = NULL;
	}
	free(head);
	return got;
}

static void
test_left_out(void)
{
	CHECK(WIRE_DEPTH_MAX == 512 && wire_array_max == 1u << 24);
	for (unsigned n = 511; n <= 514; n++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		xpc_object_t a = nest(n);
		xpc_dictionary_set_value(m, "d", a);
		xpc_release(a);
		xpc_dictionary_set_int64(m, "k", 1);
		xpc_object_t inner = xpc_dictionary_create(NULL, NULL, 0);
		a = nest(n - 1);
		xpc_dictionary_set_value(inner, "d", a);
		xpc_release(a);
		xpc_dictionary_set_int64(inner, "x", 2);
		xpc_dictionary_set_value(m, "inner", inner);
		xpc_release(inner);
		xpc_object_t got = round_trip(m);
		CHECK(got != NULL && xpc_dictionary_get_int64(got, "k") == 1);
		xpc_object_t gi = got != NULL ? xpc_dictionary_get_value(got, "inner") : NULL;
		CHECK(gi != NULL && xpc_dictionary_get_int64(gi, "x") == 2);
		if (n <= 512) {
			CHECK(got != NULL && depth_of(xpc_dictionary_get_value(got, "d")) == n);
			CHECK(gi != NULL && depth_of(xpc_dictionary_get_value(gi, "d")) == n - 1);
		} else {
			CHECK(got != NULL && xpc_dictionary_get_value(got, "d") == NULL &&
			    xpc_dictionary_get_count(got) == 2);
			CHECK(gi != NULL && xpc_dictionary_get_value(gi, "d") == NULL &&
			    xpc_dictionary_get_count(gi) == 1);
		}
		xpc_release(got);
		xpc_release(m);
	}

	wire_array_max = 16;
	for (unsigned n = 15; n <= 16; n++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		xpc_object_t a = xpc_array_create(NULL, 0);
		for (unsigned i = 0; i < n; i++) {
			xpc_array_set_int64(a, XPC_ARRAY_APPEND, i);
		}
		xpc_object_t outer = xpc_array_create(&a, 1);
		xpc_dictionary_set_value(m, "a", a);
		xpc_dictionary_set_value(m, "outer", outer);
		xpc_release(a);
		xpc_release(outer);
		xpc_dictionary_set_int64(m, "k", 3);
		xpc_object_t got = round_trip(m);
		CHECK(got != NULL && xpc_dictionary_get_int64(got, "k") == 3);
		xpc_object_t ga = got != NULL ? xpc_dictionary_get_value(got, "a") : NULL;
		xpc_object_t go = got != NULL ? xpc_dictionary_get_value(got, "outer") : NULL;
		if (n == 15) {
			CHECK(ga != NULL && xpc_array_get_count(ga) == 15 && xpc_array_get_int64(ga, 14) == 14);
			CHECK(go != NULL && xpc_array_get_count(go) == 1);
		} else {
			CHECK(ga == NULL && go == NULL && got != NULL && xpc_dictionary_get_count(got) == 1);
		}
		xpc_release(got);
		xpc_release(m);
	}

	struct enc e = { 0 };
	xpc_object_t got = NULL;
	put32(&e, T_DICTIONARY);
	put32(&e, 1);
	put_key(&e, "a");
	put32(&e, T_ARRAY);
	put32(&e, 16);
	for (int i = 0; i < 16; i++) {
		put32(&e, T_NULL);
	}
	CHECK(decode_payload(&e, false, 0, &got) == XPC_WIRE_BAD && got == NULL);
	wire_array_max = 1u << 24;

	put32(&e, T_DICTIONARY);
	put32(&e, 1);
	put_key(&e, "d");
	for (int i = 0; i < 513; i++) {
		put32(&e, T_ARRAY);
		put32(&e, 1);
	}
	put32(&e, T_ARRAY);
	put32(&e, 0);
	CHECK(decode_payload(&e, false, 0, &got) == XPC_WIRE_BAD && got == NULL);

	reset();
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t deep = nest(512);
	xpc_object_t p = xpc_array_create(NULL, 0);
	for (unsigned i = 0; i < 3; i++) {
		xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 200 + i);
		xpc_array_append_value(p, port);
		XO(port)->port = MACH_PORT_NULL;
		xpc_release(port);
	}
	xpc_array_append_value(p, deep);
	xpc_release(deep);
	xpc_dictionary_set_value(m, "p", p);
	xpc_release(p);
	xpc_object_t q = _xpc_port_create(XPC_TYPE_MACH_SEND, 300);
	xpc_dictionary_set_value(m, "q", q);
	xpc_release(q);
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	struct xpc_wire_held h;
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(h.nports == 1 && h.ports[0] == 300);
	CHECK(((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 1);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS && host.addrefs == 1);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	mach_msg_header_t *head = copy_message(host.sent);
	pseudo_receive(head);
	mach_port_t p0, p1;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && xpc_dictionary_get_value(got, "p") == NULL &&
	    XO(xpc_dictionary_get_value(got, "q"))->port == 300);
	free(head);
	xpc_release(got);
	host.deallocs = 0;
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 1 && host.dealloc_names[0] == 300);
	XO(xpc_dictionary_get_value(m, "q"))->port = MACH_PORT_NULL;
	xpc_release(m);
	reset();
}

static void
test_connect_with_port(void)
{
	struct xpc_wire_out out = message_to(6, MACH_MSG_TYPE_COPY_SEND);
	out.port0 = 40;
	out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out.port1 = 41;
	out.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	out.id = XPC_WIRE_CONNECT;
	xpc_object_t empty = xpc_dictionary_create(NULL, NULL, 0);
	for (int pass = 0; pass < 2; pass++) {
		reset();
		CHECK(_xpc_wire_send(empty, &out) == KERN_SUCCESS);
		mach_msg_header_t *old = host.sent;
		size_t pdsz = sizeof(mach_msg_port_descriptor_t);
		size_t head = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) + 2 * pdsz;
		size_t size = old->msgh_size + (pass ? pdsz : 0);
		mach_msg_header_t *m = calloc(1, size + sizeof(mach_msg_audit_trailer_t));
		memcpy(m, old, head);
		uint8_t *rest = (uint8_t *)m + head + (pass ? pdsz : 0);
		memcpy(rest, (uint8_t *)old + head, old->msgh_size - head);
		m->msgh_size = (mach_msg_size_t)size;
		if (pass) {
			((mach_msg_body_t *)(m + 1))->msgh_descriptor_count++;
			mach_msg_port_descriptor_t *extra = pds_of(m) + 2;
			extra->type = MACH_MSG_PORT_DESCRIPTOR;
			extra->disposition = MACH_MSG_TYPE_COPY_SEND;
			struct wire_hdr wh;
			memcpy(&wh, rest, sizeof(wh));
			wh.nports = 1;
			memcpy(rest, &wh, sizeof(wh));
		}
		pseudo_receive(m);
		xpc_object_t got = NULL;
		mach_port_t p0, p1;
		CHECK(_xpc_wire_decode(m, &got, &p0, &p1) == (pass ? XPC_WIRE_BAD : XPC_WIRE_OK));
		xpc_release(got);
		free(m);
	}
	xpc_release(empty);
	reset();
}

static void
test_header_wraps(void)
{
	size_t extra = 256;
	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    sizeof(mach_msg_ool_descriptor_t) + sizeof(struct wire_hdr) + extra;
	mach_msg_header_t *head = calloc(1, round_msg(size) + sizeof(mach_msg_audit_trailer_t));
	head->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	head->msgh_size = (mach_msg_size_t)size;
	head->msgh_id = XPC_WIRE_MESSAGE;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	body->msgh_descriptor_count = 1;
	mach_msg_ool_descriptor_t *od = (mach_msg_ool_descriptor_t *)(body + 1);
	od->type = MACH_MSG_OOL_DESCRIPTOR;
	struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, 0xFFFFFFFFu, 1, 0, 2 };
	memcpy(od + 1, &wh, sizeof(wh));
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(head);

	struct enc e = { 0 };
	put32(&e, T_DICTIONARY);
	put32(&e, 0);
	head = craft(e.buf, e.len, false, 0);
	head->msgh_size += 4;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(head);
	head = craft(e.buf, e.len, true, e.len);
	head->msgh_size += 4;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(head);
	head = craft(e.buf, e.len, false, 0);
	mach_msg_body_t *b = (mach_msg_body_t *)(head + 1);
	struct wire_hdr *w = (struct wire_hdr *)(b + 1);
	w->length = 0xFFFFFFFFu;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(head);
	head = craft(e.buf, e.len, false, 0);
	b = (mach_msg_body_t *)(head + 1);
	w = (struct wire_hdr *)(b + 1);
	w->ool = 2;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(head);
	head = craft(e.buf, e.len, false, 0);
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK && got != NULL);
	xpc_release(got);
	free(head);
	head = craft(NULL, 0, true, 0);
	b = (mach_msg_body_t *)(head + 1);
	od = (mach_msg_ool_descriptor_t *)(b + 1);
	free(od->address);
	od->address = NULL;
	w = (struct wire_hdr *)(od + 1);
	w->nports = 0xFFFFFFFFu;
	w->fixed = 2;
	mach_msg_header_t *exact = malloc(head->msgh_size);
	memcpy(exact, head, head->msgh_size);
	free(head);
	CHECK(_xpc_wire_decode(exact, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	free(exact);
	size_t many = XPC_WIRE_PORTS_MAX + 1;
	size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    many * sizeof(mach_msg_port_descriptor_t) + sizeof(struct wire_hdr) + e.len;
	head = calloc(1, round_msg(size) + sizeof(mach_msg_audit_trailer_t));
	head->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	head->msgh_size = (mach_msg_size_t)size;
	head->msgh_id = XPC_WIRE_MESSAGE;
	b = (mach_msg_body_t *)(head + 1);
	b->msgh_descriptor_count = (mach_msg_size_t)many;
	mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)(b + 1);
	for (size_t i = 0; i < many; i++) {
		pd[i].type = MACH_MSG_PORT_DESCRIPTOR;
		pd[i].disposition = MACH_MSG_TYPE_PORT_SEND;
	}
	w = (struct wire_hdr *)(pd + many);
	*w = (struct wire_hdr){ WIRE_MAGIC, WIRE_VERSION, (uint32_t)many, 0, (uint32_t)e.len, 0 };
	memcpy(w + 1, e.buf, e.len);
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
	w->nports = (uint32_t)many - 1;
	b->msgh_descriptor_count = (mach_msg_size_t)many - 1;
	memmove(pd + many - 1, w, sizeof(*w) + e.len);
	head->msgh_size -= sizeof(*pd);
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK && got != NULL);
	xpc_release(got);
	free(head);
	free(e.buf);
}

static void
test_connect_messages(void)
{
	struct xpc_wire_out out = message_to(6, MACH_MSG_TYPE_COPY_SEND);
	out.port0 = 40;
	out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out.port1 = 41;
	out.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	out.id = XPC_WIRE_CONNECT;
	for (int pass = 0; pass < 4; pass++) {
		reset();
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		struct xpc_wire_out o = out;
		if (pass == 1) {
			xpc_dictionary_set_int64(m, "k", 1);
		} else if (pass == 2) {
			xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 100);
			xpc_dictionary_set_value(m, "p", port);
			XO(port)->port = MACH_PORT_NULL;
			xpc_release(port);
		} else if (pass == 3) {
			o.port1 = MACH_PORT_NULL;
		}
		CHECK(_xpc_wire_send(m, &o) == KERN_SUCCESS);
		mach_msg_header_t *head = copy_message(host.sent);
		pseudo_receive(head);
		xpc_object_t got = NULL;
		mach_port_t p0, p1;
		int r = _xpc_wire_decode(head, &got, &p0, &p1);
		CHECK(r == (pass == 0 ? XPC_WIRE_OK : XPC_WIRE_BAD));
		if (r == XPC_WIRE_OK) {
			CHECK(p0 == 40 && p1 == 41 && xpc_dictionary_get_count(got) == 0);
			xpc_release(got);
		}
		free(head);
		xpc_release(m);
	}
	reset();
}

static void
test_ports_refused(void)
{
	const uint32_t n = XPC_WIRE_PORTS_MAX + 1;
	for (uint32_t fixed = 0; fixed < 4; fixed += 3) {
		size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
		    n * sizeof(mach_msg_port_descriptor_t) + sizeof(struct wire_hdr) + 8;
		mach_msg_header_t *head = calloc(1, size + sizeof(mach_msg_audit_trailer_t));
		head->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
		head->msgh_size = (mach_msg_size_t)size;
		head->msgh_id = XPC_WIRE_MESSAGE;
		mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
		body->msgh_descriptor_count = n;
		mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)(body + 1);
		for (uint32_t i = 0; i < n; i++) {
			pd[i].type = MACH_MSG_PORT_DESCRIPTOR;
			pd[i].disposition = MACH_MSG_TYPE_PORT_SEND;
		}
		if (fixed != 0) {
			pd[0].disposition = MACH_MSG_TYPE_PORT_RECEIVE;
		}
		struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, n - fixed_count(fixed), 0, 8, fixed };
		uint8_t *p = (uint8_t *)(pd + n);
		memcpy(p, &wh, sizeof(wh));
		uint32_t empty[2] = { T_DICTIONARY, 0 };
		memcpy(p + sizeof(wh), empty, sizeof(empty));
		xpc_object_t got = NULL;
		mach_port_t p0, p1;
		CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
		free(head);
	}
}

static xpc_object_t
big_entry(size_t n, bool deep)
{
	char *data = calloc(1, n);
	xpc_object_t a = xpc_array_create(NULL, 0);
	xpc_array_set_value(a, XPC_ARRAY_APPEND, xpc_data_create(data, n));
	xpc_release(xpc_array_get_value(a, 0));
	free(data);
	if (deep) {
		xpc_object_t d = nest(WIRE_DEPTH_MAX);
		xpc_array_append_value(a, d);
		xpc_release(d);
	}
	return a;
}

static void
test_size_after_left_out(void)
{
	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	wire_len_max = 100000;
	for (int pass = 0; pass < 4; pass++) {
		trap_message = xpc_dictionary_create(NULL, NULL, 0);
		xpc_object_t holder = pass == 2 ? xpc_dictionary_create(NULL, NULL, 0) : trap_message;
		xpc_object_t a = big_entry(pass == 3 ? 150000 : 120000, pass != 1);
		xpc_dictionary_set_value(holder, "big", a);
		xpc_release(a);
		xpc_dictionary_set_string(holder, "after", "tail");
		if (pass == 2) {
			xpc_dictionary_set_value(trap_message, "inner", holder);
			xpc_release(holder);
		}
		xpc_dictionary_set_int64(trap_message, "k", 7);
		if (pass == 1) {
			CHECK(send_traps());
		} else {
			CHECK(send_works());
			xpc_object_t got = round_trip(trap_message);
			xpc_object_t h = got == NULL ? NULL :
			    pass == 2 ? xpc_dictionary_get_value(got, "inner") : got;
			CHECK(got != NULL && xpc_dictionary_get_int64(got, "k") == 7);
			CHECK(h != NULL && xpc_dictionary_get_value(h, "big") == NULL &&
			    strcmp(xpc_dictionary_get_string(h, "after"), "tail") == 0);
			CHECK(host.sent != NULL && ool_of(host.sent) == NULL);
			xpc_release(got);
		}
		xpc_release(trap_message);
	}
	wire_len_max = UINT32_MAX;
	reset();
}

static void
test_length_saturates(void)
{
	struct enc e = { 0 };
	char bytes[8] = { 0 };
	e.len = wire_len_max - 2;
	put(&e, bytes, sizeof(bytes));
	CHECK(e.over == OVER_BYTES && e.len == wire_len_max - 2 && e.buf == NULL && e.cap == 0);
	e.over = 0;
	e.len = UINT64_MAX - 3;
	put(&e, bytes, sizeof(bytes));
	CHECK(e.over == OVER_BYTES && e.len == UINT64_MAX - 3 && e.buf == NULL);
	e.over = 0;
	e.len = 0;
	e.nports = XPC_WIRE_PORTS_MAX;
	put_port(&e, 5);
	CHECK(e.over == OVER_PORTS && e.nports == XPC_WIRE_PORTS_MAX && e.ports == NULL && e.len == 0);
	put(&e, bytes, sizeof(bytes));
	CHECK(e.len == 0 && e.buf == NULL);

	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	char small[16] = { 0 };
	xpc_object_t d = xpc_data_create(small, sizeof(small));
	XO(d)->buf.len = (size_t)1 << 63;
	xpc_object_t a = xpc_array_create(NULL, 0);
	for (int i = 0; i < 3; i++) {
		xpc_array_append_value(a, d);
	}
	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(trap_message, "a", a);
	CHECK(send_traps());
	xpc_release(trap_message);
	xpc_release(a);
	XO(d)->buf.len = sizeof(small);
	xpc_release(d);

	wire_len_max = 1000;
	char *data = calloc(1, 100);
	d = xpc_data_create(data, 100);
	free(data);
	xpc_object_t inner = xpc_array_create(NULL, 0);
	for (int i = 0; i < 100; i++) {
		xpc_array_append_value(inner, d);
	}
	xpc_release(d);
	for (int pass = 0; pass < 2; pass++) {
		xpc_object_t outer = xpc_array_create(NULL, 0);
		for (int i = 0; i < 100; i++) {
			xpc_array_append_value(outer, inner);
		}
		if (pass == 1) {
			xpc_object_t deep = nest(WIRE_DEPTH_MAX);
			xpc_array_append_value(outer, deep);
			xpc_release(deep);
		}
		trap_message = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(trap_message, "outer", outer);
		xpc_dictionary_set_int64(trap_message, "k", 9);
		xpc_release(outer);
		if (pass == 0) {
			CHECK(send_traps());
		} else {
			CHECK(send_works());
			xpc_object_t got = round_trip(trap_message);
			CHECK(got != NULL && xpc_dictionary_get_count(got) == 1 &&
			    xpc_dictionary_get_int64(got, "k") == 9);
			xpc_release(got);
		}
		xpc_release(trap_message);
	}
	xpc_release(inner);

	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	data = calloc(1, 2000);
	xpc_dictionary_set_data(trap_message, "a", data, 2000);
	free(data);
	xpc_object_t b = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(b, "x", 1);
	xpc_dictionary_set_value(trap_message, "b", b);
	xpc_release(b);
	CHECK(send_traps());
	xpc_release(trap_message);
	wire_len_max = UINT32_MAX;
	reset();
}

static xpc_object_t
dag(unsigned levels)
{
	xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(d, "leaf", 1);
	for (unsigned i = 0; i < levels; i++) {
		xpc_object_t up = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(up, "a", d);
		xpc_dictionary_set_value(up, "b", d);
		xpc_release(d);
		d = up;
	}
	return d;
}

static uint64_t
leaves(xpc_object_t d)
{
	if (xpc_dictionary_get_value(d, "leaf") != NULL) {
		return 1;
	}
	xpc_object_t a = xpc_dictionary_get_value(d, "a");
	xpc_object_t b = xpc_dictionary_get_value(d, "b");
	return (a != NULL ? leaves(a) : 0) + (b != NULL ? leaves(b) : 0);
}

static void
test_shared_and_cyclic(void)
{
	reset();
	double t0 = now();
	xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(d, "a", d);
	xpc_dictionary_set_value(d, "b", d);
	xpc_object_t arr = xpc_array_create(&d, 1);
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "x", d);
	xpc_dictionary_set_value(m, "arr", arr);
	xpc_object_t holder = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(holder, "c", d);
	xpc_dictionary_set_int64(holder, "y", 2);
	xpc_dictionary_set_value(m, "holder", holder);
	xpc_dictionary_set_int64(m, "k", 1);
	xpc_object_t got = round_trip(m);
	CHECK(got != NULL && xpc_dictionary_get_count(got) == 2 && xpc_dictionary_get_int64(got, "k") == 1);
	xpc_object_t gh = got != NULL ? xpc_dictionary_get_value(got, "holder") : NULL;
	CHECK(gh != NULL && xpc_dictionary_get_count(gh) == 1 && xpc_dictionary_get_int64(gh, "y") == 2);
	xpc_release(got);
	xpc_dictionary_set_value(m, "self", m);
	got = round_trip(m);
	CHECK(got != NULL && xpc_dictionary_get_value(got, "self") == NULL &&
	    xpc_dictionary_get_count(got) == 2);
	xpc_release(got);
	xpc_dictionary_set_value(m, "self", NULL);
	xpc_dictionary_set_value(d, "a", NULL);
	xpc_dictionary_set_value(d, "b", NULL);
	xpc_release(holder);
	xpc_release(arr);
	xpc_release(d);
	xpc_release(m);
	CHECK(now() - t0 < 2.0);

	d = dag(10);
	m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "d", d);
	got = round_trip(m);
	CHECK(got != NULL && leaves(xpc_dictionary_get_value(got, "d")) == 1024);
	xpc_release(got);
	xpc_release(m);
	xpc_release(d);

	t0 = now();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	wire_len_max = 100000;
	d = dag(60);
	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(trap_message, "d", d);
	CHECK(send_traps());
	xpc_object_t bad = _xpc_alloc(XPC_TYPE_CONNECTION);
	xpc_dictionary_set_value(trap_message, "z", bad);
	CHECK(send_refused());
	xpc_object_t deep = nest(WIRE_DEPTH_MAX + 1);
	xpc_array_set_value(deep, XPC_ARRAY_APPEND, bad);
	xpc_dictionary_set_value(trap_message, "z", deep);
	CHECK(send_traps());
	xpc_release(deep);
	xpc_release(trap_message);
	xpc_release(d);
	CHECK(now() - t0 < 5.0);

	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(trap_message, "a", bad);
	xpc_object_t ports = ports_dict(XPC_WIRE_PORTS_MAX + 1);
	xpc_dictionary_set_value(trap_message, "p", xpc_dictionary_get_value(ports, "ports"));
	xpc_release(ports);
	CHECK(send_refused());
	xpc_release(trap_message);
	xpc_release(bad);
	wire_len_max = UINT32_MAX;
	reset();
}

static xpc_object_t
over_ports(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t p = ports_dict(XPC_WIRE_PORTS_MAX + 1);
	xpc_dictionary_set_value(m, "a", xpc_dictionary_get_value(p, "ports"));
	xpc_release(p);
	return m;
}

static xpc_object_t
dict_chain(unsigned n, xpc_object_t bottom)
{
	xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(d, "v", bottom);
	for (unsigned i = 1; i < n; i++) {
		xpc_object_t up = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(up, "c", d);
		xpc_release(d);
		d = up;
	}
	return d;
}

static void
test_scan(void)
{
	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	xpc_object_t bad = _xpc_alloc(XPC_TYPE_CONNECTION);

	trap_message = over_ports();
	xpc_object_t arr = xpc_array_create(&bad, 1);
	xpc_dictionary_set_value(trap_message, "z", arr);
	xpc_release(arr);
	CHECK(send_refused());
	xpc_release(trap_message);

	for (uint32_t tag = T_NULL; tag <= T_MACH_SEND; tag++) {
		xpc_type_t t = wire_types[tag];
		xpc_object_t v = t == XPC_TYPE_NULL ? xpc_null_create() :
		    t == XPC_TYPE_DATA || t == XPC_TYPE_STRING ? _xpc_buf_create(t, "x", 1) :
		    t == XPC_TYPE_FD || t == XPC_TYPE_MACH_SEND ? _xpc_port_create(t, MACH_PORT_NULL) :
		    t == XPC_TYPE_ARRAY ? xpc_array_create(NULL, 0) :
		    t == XPC_TYPE_DICTIONARY ? xpc_dictionary_create(NULL, NULL, 0) : _xpc_alloc(t);
		trap_message = over_ports();
		xpc_dictionary_set_value(trap_message, "v", v);
		CHECK(send_traps());
		xpc_release(trap_message);
		if (tag < T_FD) {
			xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
			xpc_dictionary_set_value(m, "v", v);
			xpc_object_t got = round_trip(m);
			xpc_object_t gv = got != NULL ? xpc_dictionary_get_value(got, "v") : NULL;
			CHECK(gv != NULL && xpc_get_type(gv) == t);
			xpc_release(got);
			xpc_release(m);
		}
		xpc_release(v);
	}

	for (unsigned n = 511; n <= 512; n++) {
		trap_message = over_ports();
		xpc_object_t chain = dict_chain(n, bad);
		xpc_dictionary_set_value(trap_message, "c", chain);
		xpc_release(chain);
		CHECK(n == 511 ? send_refused() : send_traps());
		xpc_release(trap_message);
	}

	trap_message = over_ports();
	xpc_object_t a = nest(300);
	xpc_object_t inner = a;
	while (xpc_array_get_count(inner) > 0) {
		inner = xpc_array_get_value(inner, 0);
	}
	xpc_array_append_value(inner, bad);
	xpc_object_t shared = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(shared, "b", a);
	xpc_release(a);
	xpc_object_t deep = dict_chain(300, shared);
	xpc_dictionary_set_value(trap_message, "p", deep);
	xpc_dictionary_set_value(trap_message, "s", shared);
	xpc_release(deep);
	xpc_release(shared);
	CHECK(send_refused());
	xpc_release(trap_message);

	xpc_object_t flat = xpc_dictionary_create(NULL, NULL, 0);
	for (int i = 0; i < 2000; i++) {
		char key[16];
		snprintf(key, sizeof(key), "k%d", i);
		xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(flat, key, d);
		xpc_release(d);
	}
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t p = m;
	for (int i = 0; i < 300; i++) {
		xpc_dictionary_set_value(p, "s", flat);
		xpc_object_t q = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(p, "z", q);
		xpc_release(q);
		p = q;
	}
	char data[2000] = { 0 };
	xpc_dictionary_set_data(m, "big", data, sizeof(data));
	wire_len_max = 1000;
	struct enc e = { 0 };
	double t0 = now();
	CHECK(measure(&e, XO(m)));
	encode(&e, XO(m), 0);
	CHECK(e.over == OVER_BYTES && !unencodable(&e, XO(m)));
	CHECK(e.seen.n <= 2000 + 302 && now() - t0 < 10.0);
	enc_done(&e);
	free(e.buf);
	free(e.ports);
	trap_message = m;
	CHECK(send_traps());
	wire_len_max = UINT32_MAX;
	xpc_release(m);
	xpc_release(flat);
	xpc_release(bad);
	reset();
}

static mach_msg_header_t *
craft_ports(const void *payload, size_t len, unsigned nports)
{
	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    nports * sizeof(mach_msg_port_descriptor_t) + sizeof(struct wire_hdr) + round_msg(len);
	mach_msg_header_t *head = calloc(1, size + sizeof(mach_msg_audit_trailer_t));
	head->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	head->msgh_size = (mach_msg_size_t)size;
	head->msgh_id = XPC_WIRE_MESSAGE;
	mach_msg_body_t *body = (mach_msg_body_t *)(head + 1);
	body->msgh_descriptor_count = nports;
	mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)(body + 1);
	for (unsigned i = 0; i < nports; i++) {
		pd[i].type = MACH_MSG_PORT_DESCRIPTOR;
		pd[i].disposition = MACH_MSG_TYPE_PORT_SEND;
		pd[i].name = 100 + i;
	}
	struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, nports, 0, (uint32_t)len, 0 };
	memcpy(pd + nports, &wh, sizeof(wh));
	memcpy((uint8_t *)(pd + nports) + sizeof(wh), payload, len);
	return head;
}

static int
decode_exact(mach_msg_header_t *head, size_t size)
{
	mach_msg_header_t *exact = malloc(size);
	memcpy(exact, head, size);
	free(head);
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	int r = _xpc_wire_decode(exact, &got, &p0, &p1);
	if (r == XPC_WIRE_OK) {
		xpc_release(got);
	}
	free(exact);
	return r;
}

static int
decode_head(mach_msg_header_t *head)
{
	return decode_exact(head, head->msgh_size + sizeof(mach_msg_audit_trailer_t));
}

static int
decode_cut(mach_msg_header_t *head)
{
	return decode_exact(head, head->msgh_size);
}

static void
test_decoder_checks(void)
{
	struct enc e = { 0 };
	put32(&e, T_DICTIONARY);
	put32(&e, 0);
	CHECK(decode_head(craft(e.buf, e.len, false, 0)) == XPC_WIRE_OK);
	mach_msg_header_t *h = craft(e.buf, e.len, false, 0);
	h->msgh_bits = 0;
	CHECK(decode_head(h) == XPC_WIRE_BAD);
	h = craft(e.buf, e.len, false, 0);
	((struct wire_hdr *)((mach_msg_body_t *)(h + 1) + 1))->magic ^= 1;
	CHECK(decode_head(h) == XPC_WIRE_BAD);
	h = craft(e.buf, e.len, false, 0);
	((struct wire_hdr *)((mach_msg_body_t *)(h + 1) + 1))->version = 1;
	CHECK(decode_head(h) == XPC_WIRE_BAD);

	h = craft(e.buf, e.len, false, 0);
	h->msgh_size = sizeof(mach_msg_header_t);
	CHECK(decode_cut(h) == XPC_WIRE_BAD);
	h = craft(e.buf, e.len, false, 0);
	h->msgh_size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t);
	CHECK(decode_cut(h) == XPC_WIRE_BAD);
	h = craft(e.buf, e.len, false, 0);
	((mach_msg_body_t *)(h + 1))->msgh_descriptor_count = 1000;
	CHECK(decode_cut(h) == XPC_WIRE_BAD);

	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    sizeof(mach_msg_port_descriptor_t);
	h = calloc(1, sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    sizeof(mach_msg_ool_descriptor_t));
	h->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	h->msgh_size = (mach_msg_size_t)size;
	h->msgh_id = XPC_WIRE_MESSAGE;
	((mach_msg_body_t *)(h + 1))->msgh_descriptor_count = 1;
	((mach_msg_ool_descriptor_t *)((mach_msg_body_t *)(h + 1) + 1))->type = MACH_MSG_OOL_DESCRIPTOR;
	CHECK(decode_cut(h) == XPC_WIRE_BAD);

	size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) +
	    2 * sizeof(mach_msg_ool_descriptor_t) + sizeof(struct wire_hdr);
	h = calloc(1, size + sizeof(mach_msg_audit_trailer_t));
	h->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	h->msgh_size = (mach_msg_size_t)size;
	h->msgh_id = XPC_WIRE_MESSAGE;
	((mach_msg_body_t *)(h + 1))->msgh_descriptor_count = 2;
	mach_msg_ool_descriptor_t *od = (mach_msg_ool_descriptor_t *)((mach_msg_body_t *)(h + 1) + 1);
	od[0].type = od[1].type = MACH_MSG_OOL_DESCRIPTOR;
	od[1].address = e.buf;
	od[1].size = (mach_msg_size_t)e.len;
	struct wire_hdr wh = { WIRE_MAGIC, WIRE_VERSION, 0, 1, (uint32_t)e.len, 0 };
	memcpy(od + 2, &wh, sizeof(wh));
	mach_msg_header_t *one = calloc(1, size - sizeof(*od) + sizeof(mach_msg_audit_trailer_t));
	memcpy(one, h, sizeof(*h) + sizeof(mach_msg_body_t));
	one->msgh_size = (mach_msg_size_t)(size - sizeof(*od));
	((mach_msg_body_t *)(one + 1))->msgh_descriptor_count = 1;
	memcpy((mach_msg_body_t *)(one + 1) + 1, od + 1, sizeof(*od) + sizeof(wh));
	void *copy = malloc(e.len);
	memcpy(copy, e.buf, e.len);
	((mach_msg_ool_descriptor_t *)((mach_msg_body_t *)(one + 1) + 1))->address = copy;
	CHECK(decode_head(one) == XPC_WIRE_OK);
	od[1].address = malloc(e.len);
	memcpy(od[1].address, e.buf, e.len);
	CHECK(decode_head(h) == XPC_WIRE_BAD);
	free(e.buf);

	memset(&e, 0, sizeof(e));
	put32(&e, T_DICTIONARY);
	put32(&e, 2);
	put_key(&e, "a");
	put32(&e, T_MACH_SEND);
	put32(&e, 0);
	put_key(&e, "b");
	put32(&e, T_MACH_SEND);
	put32(&e, 0);
	CHECK(decode_head(craft_ports(e.buf, e.len, 1)) == XPC_WIRE_BAD);
	CHECK(decode_head(craft_ports(e.buf, e.len, 2)) == XPC_WIRE_BAD);
	memcpy(e.buf + e.len - 4, &(uint32_t){ 1 }, 4);
	CHECK(decode_head(craft_ports(e.buf, e.len, 2)) == XPC_WIRE_OK);
	memcpy(e.buf + e.len - 4, &(uint32_t){ 2 }, 4);
	CHECK(decode_head(craft_ports(e.buf, e.len, 2)) == XPC_WIRE_BAD);
	free(e.buf);

	memset(&e, 0, sizeof(e));
	put32(&e, T_ARRAY);
	put32(&e, 0);
	CHECK(decode_head(craft(e.buf, e.len, false, 0)) == XPC_WIRE_BAD);
	free(e.buf);
	memset(&e, 0, sizeof(e));
	put32(&e, T_DICTIONARY);
	put32(&e, 0);
	put32(&e, T_NULL);
	CHECK(decode_head(craft(e.buf, e.len, false, 0)) == XPC_WIRE_BAD);
	free(e.buf);
	for (int depth = 512; depth <= 513; depth++) {
		memset(&e, 0, sizeof(e));
		put32(&e, T_DICTIONARY);
		put32(&e, 1);
		put_key(&e, "d");
		for (int i = 1; i < depth; i++) {
			put32(&e, T_ARRAY);
			put32(&e, 1);
		}
		put32(&e, T_NULL);
		CHECK(decode_head(craft(e.buf, e.len, false, 0)) == (depth == 512 ? XPC_WIRE_OK : XPC_WIRE_BAD));
		free(e.buf);
	}
}

static void
test_pass_allocations(void)
{
#ifndef TEST_ASAN
	reset();
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t chain = nest(100);
	xpc_dictionary_set_value(m, "c", chain);
	xpc_release(chain);
	xpc_dictionary_set_int64(m, "n", 1);
	size_t sizes[] = { 128 * sizeof(struct frame), 128 * sizeof(const struct xpc_obj *) };
	for (int i = 0; i < 3; i++) {
		host.alloc_failed = 0;
		if (i < 2) {
			host.fail_realloc_size = sizes[i];
		} else {
			host.fail_calloc = 1;
		}
		CHECK(_xpc_wire_send(m, &out) == KERN_INVALID_ARGUMENT);
		CHECK(host.alloc_failed == 1 && host.sends == 0);
		host.fail_realloc_size = host.fail_calloc = 0;
	}
	CHECK(_xpc_wire_send(m, &out) == KERN_SUCCESS && host.sends == 1);
	xpc_release(m);

	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	trap_message = over_ports();
	for (int i = 0; i < 2; i++) {
		host.fail_calloc = i == 0 ? 1 : 128 * sizeof(struct shape);
		host.fail_calloc_skip = 1;
		CHECK(send_refused());
		host.fail_calloc = 0;
		host.fail_calloc_skip = 0;
		CHECK(send_traps());
		for (int k = 0; k < 40; k++) {
			char key[16];
			snprintf(key, sizeof(key), "d%d", k);
			xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
			xpc_dictionary_set_value(trap_message, key, d);
			xpc_release(d);
		}
	}
	xpc_release(trap_message);
	reset();
#endif
}

static void
test_scan_order(void)
{
	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	xpc_object_t bad = _xpc_alloc(XPC_TYPE_CONNECTION);
	for (int order = 0; order < 2; order++) {
		xpc_object_t x = xpc_dictionary_create(NULL, NULL, 0);
		xpc_object_t w = xpc_array_create(&bad, 1);
		xpc_dictionary_set_value(x, "w", w);
		xpc_release(w);
		xpc_object_t y = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(y, "x", x);
		xpc_object_t chain = dict_chain(511, x);
		xpc_release(x);
		trap_message = over_ports();
		if (order == 0) {
			xpc_dictionary_set_value(trap_message, "c", y);
			xpc_dictionary_set_value(trap_message, "b", chain);
		} else {
			xpc_dictionary_set_value(trap_message, "c", chain);
			xpc_dictionary_set_value(trap_message, "b", y);
		}
		xpc_release(y);
		xpc_release(chain);
		CHECK(send_refused());
		xpc_release(trap_message);
	}
	xpc_release(bad);
	reset();
}

static void
test_scan_allocations(void)
{
#ifndef TEST_ASAN
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	for (int i = 0; i < 70; i++) {
		char key[16];
		snprintf(key, sizeof(key), "d%d", i);
		xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(m, key, d);
		xpc_release(d);
	}
	char data[200] = { 0 };
	xpc_dictionary_set_data(m, "big", data, sizeof(data));
	wire_len_max = 100;
	for (int i = 0; i < 3; i++) {
		struct enc e = { 0 };
		CHECK(measure(&e, XO(m)));
		encode(&e, XO(m), 0);
		CHECK(e.over == OVER_BYTES);
		host.alloc_failed = 0;
		if (i == 1) {
			host.fail_malloc = 64 * sizeof(struct reach);
		} else if (i == 2) {
			host.fail_realloc_size = 128 * sizeof(struct reach);
		}
		CHECK(unencodable(&e, XO(m)) == (i != 0));
		CHECK(host.alloc_failed == (i != 0) && host.fail_malloc == 0 && host.fail_realloc_size == 0);
		host.fail_malloc = 0;
		host.fail_realloc_size = 0;
		CHECK(i != 2 || host.realloc_kept_freed);
		CHECK(e.seen.n == (i == 0 ? 71 : i == 1 ? 0 : 64));
		enc_done(&e);
		free(e.buf);
		free(e.ports);
	}
	wire_len_max = UINT32_MAX;
	xpc_release(m);
#endif
}

static void
test_truncated(void)
{
	for (int c = 0; c < 6; c++) {
		struct enc e = { 0 };
		put32(&e, T_DICTIONARY);
		if (c == 1) {
			put32(&e, 1);
		} else if (c >= 2) {
			put32(&e, 1);
			put32(&e, c == 2 ? 100 : 4);
			if (c >= 3) {
				put(&e, "kkkk", 4);
				put32(&e, c == 3 ? T_STRING : c == 4 ? T_DATA : T_STRING);
				put32(&e, c == 5 ? 3 : 100);
				if (c == 5) {
					put(&e, "abc", 3);
				}
			}
		}
		mach_msg_header_t *h = craft(e.buf, e.len, false, 0);
		CHECK(c == 5 ? decode_head(h) == XPC_WIRE_OK : decode_cut(h) == XPC_WIRE_BAD);
		free(e.buf);
	}
	size_t size = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t) + sizeof(struct wire_hdr) - 1;
	mach_msg_header_t *h = calloc(1, size);
	h->msgh_bits = MACH_MSGH_BITS_SET(0, 0, 0, MACH_MSGH_BITS_COMPLEX);
	h->msgh_size = (mach_msg_size_t)size;
	h->msgh_id = XPC_WIRE_MESSAGE;
	CHECK(decode_cut(h) == XPC_WIRE_BAD);
}

static void
test_fd_round_trip(void)
{
	for (int inarr = 0; inarr < 2; inarr++) {
		xpc_type_t types[] = { XPC_TYPE_FD, XPC_TYPE_MACH_SEND };
		for (int t = 0; t < 2; t++) {
			xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
			xpc_object_t p = _xpc_port_create(types[t], 77);
			if (inarr) {
				xpc_object_t a = xpc_array_create(&p, 1);
				xpc_dictionary_set_value(m, "p", a);
				xpc_release(a);
			} else {
				xpc_dictionary_set_value(m, "p", p);
			}
			xpc_object_t got = round_trip(m);
			xpc_object_t gv = got != NULL ? xpc_dictionary_get_value(got, "p") : NULL;
			if (inarr && gv != NULL) {
				gv = xpc_get_type(gv) == XPC_TYPE_ARRAY ? xpc_array_get_value(gv, 0) : NULL;
			}
			CHECK(gv != NULL && xpc_get_type(gv) == types[t] && XO(gv)->port == 77);
			xpc_release(got);
			XO(p)->port = MACH_PORT_NULL;
			xpc_release(p);
			xpc_release(m);
		}
	}
}

static void
test_height_limits(void)
{
	xpc_object_t inner = xpc_array_create(NULL, 0);
	xpc_object_t outer = xpc_array_create(&inner, 1);
	uint32_t heights[] = { HEIGHT_INF - 2, HEIGHT_INF - 1, HEIGHT_INF, UINT32_MAX };
	uint32_t want[] = { HEIGHT_INF - 1, HEIGHT_INF, HEIGHT_INF, HEIGHT_INF };
	for (size_t i = 0; i < 4; i++) {
		struct enc e = { 0 };
		memo_add(&e.shapes, XO(inner))->height = heights[i];
		struct shape *s = memo_add(&e.shapes, XO(outer));
		settle(&e, s);
		CHECK(s->height == want[i] && !s->big);
		CHECK(fits(&e, XO(outer), 0) == (i == 0) && !fits(&e, XO(outer), 1));
		enc_done(&e);
	}
	struct enc e = { 0 };
	struct shape *s = memo_add(&e.shapes, XO(inner));
	s->height = UINT32_MAX;
	CHECK(!fits(&e, XO(inner), 1) && !fits(&e, XO(inner), WIRE_DEPTH_MAX));
	s->height = 0;
	CHECK(fits(&e, XO(inner), WIRE_DEPTH_MAX) && !fits(&e, XO(inner), WIRE_DEPTH_MAX + 1));
	enc_done(&e);
	xpc_release(outer);
	xpc_release(inner);
}

static void
test_memo_spread(void)
{
	struct memo m = { 0 };
	size_t n = 1u << 14;
	for (size_t i = 0; i < n; i++) {
		uintptr_t p = ((uintptr_t)1 << 32) + (uintptr_t)i * ((uintptr_t)1 << 24);
		CHECK(memo_add(&m, (const struct xpc_obj *)p) != NULL);
	}
	size_t worst = 0, found = 0;
	for (size_t i = 0; i < m.cap; i++) {
		if (m.v[i].o == NULL) {
			continue;
		}
		size_t d = (i - memo_hash(m.v[i].o, m.cap)) & (m.cap - 1);
		worst = d > worst ? d : worst;
	}
	for (size_t i = 0; i < n; i++) {
		uintptr_t p = ((uintptr_t)1 << 32) + (uintptr_t)i * ((uintptr_t)1 << 24);
		found += memo_find(&m, (const struct xpc_obj *)p) != NULL;
	}
	CHECK(m.n == n && found == n && worst < 64);
	free(m.v);
}

static bool
only_n(xpc_object_t m)
{
	xpc_object_t got = round_trip(m);
	bool ok = got != NULL && xpc_dictionary_get_count(got) == 1 &&
	    xpc_dictionary_get_int64(got, "n") == 1;
	xpc_release(got);
	return ok;
}

static void
test_cycle_members(void)
{
	xpc_object_t a = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t b = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t c = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(a, "b", b);
	xpc_dictionary_set_value(a, "c", c);
	xpc_dictionary_set_value(b, "a", a);
	xpc_dictionary_set_value(c, "b", b);
	xpc_dictionary_set_int64(c, "keep", 1);
	for (int order = 0; order < 3; order++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		if (order == 0) {
			xpc_dictionary_set_value(m, "a", a);
			xpc_dictionary_set_value(m, "c", c);
		} else if (order == 1) {
			xpc_dictionary_set_value(m, "c", c);
			xpc_dictionary_set_value(m, "a", a);
		} else {
			xpc_dictionary_set_value(m, "c", c);
		}
		xpc_dictionary_set_int64(m, "n", 1);
		CHECK(only_n(m));
		xpc_release(m);
	}
	xpc_dictionary_set_value(a, "b", NULL);
	xpc_dictionary_set_value(a, "c", NULL);

	xpc_object_t x = xpc_array_create(NULL, 0);
	xpc_object_t y = xpc_array_create(&x, 1);
	xpc_array_append_value(x, y);
	xpc_object_t self = xpc_array_create(NULL, 0);
	xpc_array_append_value(self, self);
	xpc_object_t p = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t q = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(p, "q", q);
	xpc_dictionary_set_value(q, "p", p);
	xpc_object_t mx = xpc_array_create(NULL, 0);
	xpc_object_t md = xpc_dictionary_create(NULL, NULL, 0);
	xpc_array_append_value(mx, md);
	xpc_dictionary_set_value(md, "x", mx);
	xpc_object_t cycles[] = { x, y, self, p, q, mx, md };
	for (size_t i = 0; i < sizeof(cycles) / sizeof(cycles[0]); i++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_value(m, "v", cycles[i]);
		xpc_dictionary_set_int64(m, "n", 1);
		CHECK(only_n(m));
		xpc_release(m);
	}

	xpc_object_t u = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(u, "p", p);
	xpc_object_t v = xpc_array_create(&q, 1);
	xpc_object_t w = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(w, "q", q);
	xpc_dictionary_set_int64(w, "z", 1);
	for (int order = 0; order < 2; order++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		const char *keys[] = { "u", "v", "w" };
		xpc_object_t vals[] = { u, v, w };
		for (int i = 0; i < 3; i++) {
			int k = order == 0 ? i : 2 - i;
			xpc_dictionary_set_value(m, keys[k], vals[k]);
		}
		xpc_object_t got = round_trip(m);
		xpc_object_t gu = got != NULL ? xpc_dictionary_get_value(got, "u") : NULL;
		xpc_object_t gw = got != NULL ? xpc_dictionary_get_value(got, "w") : NULL;
		CHECK(got != NULL && xpc_dictionary_get_count(got) == 2 && gu != NULL &&
		    xpc_dictionary_get_count(gu) == 0 && gw != NULL && xpc_dictionary_get_count(gw) == 1 &&
		    xpc_dictionary_get_int64(gw, "z") == 1);
		xpc_release(got);
		xpc_release(m);
	}
	xpc_object_t null = xpc_null_create();
	xpc_array_set_value(x, 0, null);
	xpc_array_set_value(self, 0, null);
	xpc_dictionary_set_value(p, "q", NULL);
	xpc_array_set_value(mx, 0, null);
	xpc_object_t all[] = { a, b, c, x, y, self, p, q, mx, md, u, v, w };
	for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
		xpc_release(all[i]);
	}

	size_t n = 200000;
	xpc_object_t *chain = calloc(n, sizeof(*chain));
	for (size_t i = 0; i < n; i++) {
		chain[i] = xpc_array_create(NULL, 0);
	}
	for (size_t i = 0; i < n; i++) {
		if (i + 1 < n) {
			xpc_array_append_value(chain[i], chain[i + 1]);
		}
		xpc_array_append_value(chain[i], chain[0]);
	}
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "c", chain[0]);
	xpc_dictionary_set_int64(m, "n", 1);
	double t0 = now();
	CHECK(only_n(m));
	CHECK(now() - t0 < 2.0);
	xpc_release(m);
	for (size_t i = 0; i < n; i++) {
		xpc_array_set_value(chain[i], 0, null);
		if (i + 1 < n) {
			xpc_array_set_value(chain[i], 1, null);
		}
	}
	for (size_t i = 0; i < n; i++) {
		xpc_release(chain[i]);
	}
	free(chain);

	xpc_object_t level = xpc_array_create(NULL, 0);
	xpc_array_append_value(level, null);
	for (int i = 0; i < 10; i++) {
		xpc_object_t two[] = { level, level };
		xpc_object_t up = xpc_array_create(two, 2);
		xpc_release(level);
		level = up;
	}
	m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "l", level);
	xpc_dictionary_set_value(m, "again", level);
	xpc_object_t got = round_trip(m);
	size_t leaves_seen = 0;
	for (int k = 0; k < 2 && got != NULL; k++) {
		xpc_object_t v0 = xpc_dictionary_get_value(got, k == 0 ? "l" : "again");
		for (size_t i = 0; v0 != NULL && i < 1024; i++) {
			xpc_object_t t = v0;
			for (int d = 9; d >= 0 && t != NULL; d--) {
				t = xpc_array_get_count(t) == 2 ? xpc_array_get_value(t, (i >> d) & 1) : NULL;
			}
			leaves_seen += t != NULL && xpc_array_get_count(t) == 1 &&
			    xpc_get_type(xpc_array_get_value(t, 0)) == XPC_TYPE_NULL;
		}
	}
	CHECK(leaves_seen == 2048);
	xpc_release(got);
	xpc_release(m);
	xpc_release(level);

	xpc_object_t pair[] = { nest(WIRE_DEPTH_MAX), xpc_array_create(NULL, 0) };
	xpc_object_t first_deep = xpc_array_create(pair, 2);
	xpc_release(pair[0]);
	xpc_release(pair[1]);
	m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "d", first_deep);
	xpc_dictionary_set_int64(m, "n", 1);
	CHECK(only_n(m));
	xpc_release(m);
	xpc_release(first_deep);
}

static void
test_last_levels(void)
{
	xpc_object_t inner = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(inner, "k", 1);
	xpc_object_t a = xpc_array_create(&inner, 1);
	xpc_release(inner);
	for (unsigned i = 1; i < WIRE_DEPTH_MAX - 1; i++) {
		xpc_object_t b = xpc_array_create(&a, 1);
		xpc_release(a);
		a = b;
	}
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(m, "d", a);
	xpc_release(a);
	xpc_object_t got = round_trip(m);
	CHECK(got != NULL);
	xpc_object_t v = got != NULL ? xpc_dictionary_get_value(got, "d") : NULL;
	unsigned levels = 0;
	while (v != NULL && xpc_get_type(v) == XPC_TYPE_ARRAY) {
		levels++;
		v = xpc_array_get_value(v, 0);
	}
	CHECK(levels == WIRE_DEPTH_MAX - 1 && v != NULL && xpc_get_type(v) == XPC_TYPE_DICTIONARY &&
	    xpc_dictionary_get_count(v) == 0);
	xpc_release(got);
	xpc_release(m);

	reset();
	trap_out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	wire_len_max = 515;
	char data[482] = { 0 };
	trap_message = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_data(trap_message, "a", data, sizeof(data));
	xpc_object_t b = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(trap_message, "b", b);
	xpc_release(b);
	CHECK(send_traps());
	xpc_release(trap_message);
	wire_len_max = UINT32_MAX;
	reset();
}

static void
test_large_receive(void)
{
	mach_msg_header_t *head = NULL;
	reset();
	char *big = calloc(1, 300000);
	enqueue(craft(big, 300000, false, 0), 1);
	enqueue(craft(big, 30000, false, 0), 1);
	free(big);
	CHECK(_xpc_wire_receive(7, MACH_RCV_TIMEOUT, 0, &head) == KERN_SUCCESS);
	CHECK(head != NULL && head->msgh_size > 300000 && host.queued == 1);
	CHECK(_xpc_wire_audit_token(head) != NULL);
	free(head);
	CHECK(_xpc_wire_receive(7, MACH_RCV_TIMEOUT, 0, &head) == KERN_SUCCESS);
	CHECK(head != NULL && head->msgh_size > 30000);
	free(head);
	CHECK(_xpc_wire_receive(7, MACH_RCV_TIMEOUT, 0, &head) == MACH_RCV_TIMED_OUT);
	CHECK(host.receive_sizes[0] < 300000 && host.receive_sizes[1] > 300000);

	reset();
	char *small = calloc(1, 64);
	enqueue(craft(small, 64, true, 64), 1);
	free(small);
	host.body_error = true;
	head = NULL;
	CHECK(_xpc_wire_receive(7, MACH_RCV_TIMEOUT, 0, &head) == MACH_RCV_BODY_ERROR);
	CHECK(head == NULL && host.queued == 0 && host.destroys == 1 && host.vm_deallocs == 1);
	reset();
}

static void
test_reply_ports(void)
{
	static const mach_msg_type_name_t types[] = {
		MACH_MSG_TYPE_PORT_SEND_ONCE, MACH_MSG_TYPE_PORT_SEND,
	};
	for (int request = 0; request < 2; request++) {
		for (size_t i = 0; i < 2; i++) {
			reset();
			struct enc e = { 0 };
			put32(&e, T_DICTIONARY);
			put32(&e, 0);
			mach_msg_header_t *head = craft(e.buf, e.len, false, 0);
			free(e.buf);
			head->msgh_id = request ? XPC_WIRE_REQUEST : XPC_WIRE_REPLY;
			head->msgh_remote_port = 55;
			head->msgh_bits = MACH_MSGH_BITS_SET(types[i], 0, 0, MACH_MSGH_BITS_COMPLEX);
			xpc_object_t got = NULL;
			mach_port_t p0, p1;
			int r = _xpc_wire_decode(head, &got, &p0, &p1);
			free(head);
			bool kept = i == 0;
			CHECK(r == (request && !kept ? XPC_WIRE_BAD : XPC_WIRE_OK));
			if (r != XPC_WIRE_OK) {
				continue;
			}
			struct xpc_msginfo *m = _xpc_dictionary_msginfo(got, false);
			CHECK(m != NULL && m->reply == (kept ? 55u : MACH_PORT_NULL));
			CHECK(host.deallocs == (kept ? 0 : 1));
			xpc_object_t reply = xpc_dictionary_create_reply(got);
			CHECK((reply != NULL) == kept);
			if (reply != NULL) {
				CHECK(_xpc_dictionary_take_reply(reply) == 55);
				xpc_release(reply);
			}
			xpc_release(got);
		}
	}
}

static void
test_objects(void)
{
	xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_double(d, "half", 0.5);
	xpc_dictionary_set_int64(d, "int", 3);
	CHECK(xpc_dictionary_get_double(d, "half") == 0.5);
	CHECK(isnan(xpc_dictionary_get_double(d, "int")));
	CHECK(isnan(xpc_dictionary_get_double(d, "missing")));
	xpc_object_t a = xpc_array_create(NULL, 0);
	xpc_object_t quarter = xpc_double_create(0.25);
	xpc_array_append_value(a, quarter);
	xpc_release(quarter);
	xpc_array_set_int64(a, XPC_ARRAY_APPEND, 1);
	CHECK(xpc_array_get_double(a, 0) == 0.25);
	CHECK(isnan(xpc_array_get_double(a, 1)));
	CHECK(isnan(xpc_array_get_double(a, 2)));
	xpc_release(a);

	xpc_object_t zero = xpc_double_create(0.0), negzero = xpc_double_create(-0.0);
	CHECK(xpc_equal(zero, negzero) && xpc_hash(zero) == xpc_hash(negzero));
	xpc_object_t one = xpc_double_create(1.0);
	CHECK(xpc_hash(one) != xpc_hash(zero));
	xpc_release(one);
	xpc_release(zero);
	xpc_release(negzero);

	reset();
	xpc_object_t fd = _xpc_port_create(XPC_TYPE_FD, 300);
	xpc_object_t copy = xpc_copy(fd);
	CHECK(copy != NULL && copy != fd && xpc_get_type(copy) == XPC_TYPE_FD);
	CHECK(copy != NULL && XO(copy)->port == 300 && host.addrefs == 1);
	CHECK(xpc_equal(fd, fd) && !xpc_equal(fd, copy) && xpc_hash(fd) == xpc_hash(fd));
	xpc_object_t same = _xpc_port_create(XPC_TYPE_FD, 300);
	CHECK(!xpc_equal(fd, same) && xpc_hash(fd) != xpc_hash(same));
	XO(same)->port = MACH_PORT_NULL;
	xpc_release(same);
	xpc_release(copy);
	CHECK(host.deallocs == 1);
	host.fail_addref = true;
	CHECK(xpc_copy(fd) == NULL);
	host.fail_addref = false;
	xpc_release(fd);
	CHECK(host.deallocs == 2);

	xpc_object_t s = xpc_string_create_with_format("a%cb", 0);
	CHECK(xpc_string_get_length(s) == 1 && strcmp(xpc_string_get_string_ptr(s), "a") == 0);
	xpc_dictionary_set_value(d, "s", s);
	xpc_release(s);
	reset();
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	CHECK(_xpc_wire_send(d, &out) == KERN_SUCCESS);
	mach_msg_header_t *head = copy_message(host.sent);
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	CHECK(_xpc_wire_decode(head, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && strcmp(xpc_dictionary_get_string(got, "s"), "a") == 0);
	xpc_release(got);
	free(head);
	xpc_release(d);
}

static void
test_failed_sends(void)
{
	static const kern_return_t kept[] = {
		KERN_INVALID_ARGUMENT, KERN_RESOURCE_SHORTAGE, MACH_SEND_MSG_TOO_SMALL,
		MACH_SEND_TOO_LARGE, MACH_SEND_INVALID_TYPE, MACH_SEND_INVALID_DATA,
		MACH_SEND_INVALID_HEADER, MACH_SEND_INVALID_DEST, MACH_SEND_INVALID_VOUCHER,
	};
	static const kern_return_t taken[] = {
		KERN_SUCCESS, MACH_SEND_TIMED_OUT, MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE,
		MACH_SEND_INVALID_RIGHT, MACH_SEND_INVALID_MEMORY, MACH_SEND_NO_BUFFER,
		MACH_SEND_INVALID_REPLY,
	};
	for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++) {
		CHECK(_xpc_wire_kept(kept[i]));
	}
	for (size_t i = 0; i < sizeof(taken) / sizeof(taken[0]); i++) {
		CHECK(!_xpc_wire_kept(taken[i]));
	}

	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_MOVE_SEND_ONCE);
	static const kern_return_t results[] = { MACH_SEND_INVALID_DEST, MACH_SEND_INVALID_RIGHT };
	for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		reset();
		host.send_result = results[i];
		CHECK(_xpc_wire_send(m, &out) == results[i]);
		CHECK(host.destroys == 0);
		CHECK(host.deallocs == 0);
	}
	xpc_release(m);
}

static void
test_returned_messages(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	char *data = calloc(1, 100000);
	xpc_dictionary_set_data(m, "big", data, 100000);
	free(data);

	reset();
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.reply = 9;
	out.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
	out.id = XPC_WIRE_REQUEST;
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_TIMED_OUT);
	CHECK(host.destroys == 1);
	CHECK(host.vm_deallocs == 1);
	CHECK(host.deallocs == 1 && host.dealloc_names[0] == 9);

	reset();
	host.send_result = MACH_SEND_NO_BUFFER;
	host.pseudo = true;
	host.reply_port = true;
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_NO_BUFFER);
	CHECK(host.destroys == 1 && MACH_MSGH_BITS_LOCAL(host.destroy_bits) == 0);
	CHECK(host.deallocs == 0);

	reset();
	host.send_result = MACH_SEND_NO_BUFFER;
	host.pseudo = true;
	out = message_to(7, MACH_MSG_TYPE_MOVE_SEND_ONCE);
	out.id = XPC_WIRE_REPLY;
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_NO_BUFFER);
	CHECK(host.destroys == 1);
	CHECK(host.vm_deallocs == 1);
	CHECK(!_xpc_wire_kept(MACH_SEND_NO_BUFFER));

	reset();
	host.send_result = MACH_SEND_NO_BUFFER;
	out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.reply = 9;
	out.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
	out.id = XPC_WIRE_REQUEST;
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_NO_BUFFER);
	CHECK(host.destroys == 1 && MACH_MSGH_BITS_LOCAL(host.destroy_bits) == 0);
	CHECK(host.vm_deallocs == 0);
	CHECK(host.deallocs == 0);

	mach_msg_header_t head = {
		.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_MOVE_SEND_ONCE, MACH_MSG_TYPE_MAKE_SEND_ONCE,
		    0, 0),
		.msgh_size = sizeof(head),
		.msgh_remote_port = 7,
		.msgh_local_port = 9,
	};
	reset();
	_xpc_wire_destroy(&head);
	CHECK(host.destroys == 1 && host.deallocs == 0);
	head.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_MOVE_SEND, MACH_MSG_TYPE_MOVE_SEND_ONCE, 0, 0);
	reset();
	_xpc_wire_destroy(&head);
	CHECK(host.destroys == 1 && host.deallocs == 1 && host.dealloc_names[0] == 9);

	xpc_release(m);
}

static mach_msg_port_descriptor_t *
ports_of(mach_msg_header_t *head)
{
	return (mach_msg_port_descriptor_t *)((mach_msg_body_t *)(head + 1) + 1);
}

static xpc_object_t
queued_message(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(m, "k", "v");
	xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 100);
	xpc_dictionary_set_value(m, "port", port);
	xpc_release(port);
	char *data = calloc(1, 100000);
	xpc_dictionary_set_data(m, "big", data, 100000);
	free(data);
	return m;
}

static struct xpc_wire_out
request_to(mach_port_t dest)
{
	struct xpc_wire_out out = message_to(dest, MACH_MSG_TYPE_COPY_SEND);
	out.reply = 9;
	out.reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
	out.id = XPC_WIRE_REQUEST;
	return out;
}

static void
test_queued_messages(void)
{
	xpc_object_t m = queued_message();
	struct xpc_wire_out out = request_to(5);
	struct xpc_wire_held h;

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(host.sends == 0 && host.addrefs == 0);
	mach_msg_ool_descriptor_t *od = ool_of(h.head);
	CHECK(od != NULL && !od->deallocate && od->address == h.ool);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	CHECK(MACH_MSGH_BITS_REMOTE(host.sent->msgh_bits) == MACH_MSG_TYPE_COPY_SEND);
	CHECK(MACH_MSGH_BITS_LOCAL(host.sent->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == 0);
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 0 && host.destroys == 0 && host.vm_deallocs == 0);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_TIMED_OUT);
	CHECK(host.destroys == 1 && host.vm_deallocs == 1 && host.deallocs == 0);
	CHECK(MACH_MSGH_BITS_REMOTE(h.head->msgh_bits) == MACH_MSG_TYPE_COPY_SEND);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MOVE_SEND_ONCE);
	CHECK(h.head->msgh_local_port == 9);
	CHECK(ports_of(h.head)[0].disposition == MACH_MSG_TYPE_COPY_SEND);
	CHECK(ool_of(h.head)->address == h.ool);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	CHECK(host.addrefs == 1 && h.head->msgh_local_port == 9);
	CHECK(_xpc_wire_try(&h) == MACH_SEND_TIMED_OUT);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MOVE_SEND_ONCE);
	host.send_result = KERN_SUCCESS;
	host.pseudo = false;
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	mach_msg_header_t *copy = copy_message(host.sent);
	copy->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSGH_BITS_LOCAL(copy->msgh_bits), 0, 0,
	    MACH_MSGH_BITS_COMPLEX);
	copy->msgh_remote_port = copy->msgh_local_port;
	copy->msgh_local_port = MACH_PORT_NULL;
	for (int i = 0; i < 3; i++) {
		ports_of(copy)[i].disposition = received(ports_of(copy)[i].disposition);
	}
	CHECK(_xpc_wire_decode(copy, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && xpc_equal(m, got));
	xpc_release(got);
	free(copy);
	host.deallocs = 0;
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 1 && host.dealloc_names[0] == 100);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	CHECK(h.head->msgh_local_port == 9);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	host.send_result = MACH_SEND_INVALID_DEST;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_INVALID_DEST);
	host.send_result = KERN_SUCCESS;
	CHECK(host.sends == 1);
	_xpc_wire_release(&h);
	CHECK(host.destroys == 0 && host.vm_deallocs == 0);
	CHECK(host.deallocs == 1 && host.dealloc_names[0] == 100);
	CHECK(host.sends == 2 && host.sent->msgh_id == XPC_WIRE_WAKE && host.sent->msgh_remote_port == 9);
	CHECK(MACH_MSGH_BITS_REMOTE(host.sent->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	_xpc_wire_release(&h);
	CHECK(host.sends == 0);

	static const kern_return_t later[] = {
		MACH_SEND_INVALID_RIGHT, MACH_SEND_INVALID_MEMORY, MACH_SEND_INVALID_REPLY,
		MACH_SEND_INVALID_OPTIONS, MACH_SEND_MSG_FILTERED,
	};
	for (size_t i = 0; i < sizeof(later) / sizeof(later[0]); i++) {
		reset();
		CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
		CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
		host.send_result = later[i];
		CHECK(_xpc_wire_try(&h) == later[i]);
		CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == 0);
		_xpc_wire_release(&h);
		CHECK(host.destroys == 0 && host.vm_deallocs == 0);
		CHECK(host.deallocs == 1 && host.dealloc_names[0] == 100);
	}

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	host.reply_port = true;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_TIMED_OUT);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	CHECK(h.head->msgh_local_port == 9 && host.deallocs == 0);
	host.send_result = KERN_SUCCESS;
	host.pseudo = false;
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	CHECK(MACH_MSGH_BITS_LOCAL(host.sent->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	CHECK(host.sent->msgh_local_port == 9);
	_xpc_wire_release(&h);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	host.reply_port = true;
	host.rename_reply = true;
	CHECK(_xpc_wire_try(&h) == (MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE));
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == 0);
	_xpc_wire_release(&h);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	host.lose_reply = true;
	CHECK(_xpc_wire_try(&h) == (MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE));
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == 0);
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 0);

#ifndef TEST_ASAN
	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	host.fail_malloc = h.head->msgh_size;
	CHECK(_xpc_wire_try(&h) == KERN_RESOURCE_SHORTAGE && _xpc_wire_kept(KERN_RESOURCE_SHORTAGE));
	CHECK(host.sends == 0 && host.fail_malloc == 0);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	CHECK(h.head->msgh_local_port == 9 && h.receive == MACH_PORT_NULL);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS && host.sends == 1);
	_xpc_wire_release(&h);
#endif

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_NO_BUFFER;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_NO_BUFFER);
	CHECK(host.destroys == 0);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 0 && host.vm_deallocs == 0);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.fail_addref = true;
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	CHECK(h.ports[0] == MACH_PORT_NULL);
	CHECK(ports_of(h.head)[0].name == MACH_PORT_NULL);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	CHECK(ports_of(host.sent)[0].name == MACH_PORT_NULL);
	host.deallocs = 0;
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 0);

	xpc_object_t small = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(small, "k", "v");
	reset();
	CHECK(_xpc_wire_prepare(small, &out, &h) == KERN_SUCCESS);
	CHECK(h.ool == NULL && ool_of(h.head) == NULL);
	_xpc_wire_release(&h);
	xpc_release(small);

	xpc_release(m);
}

static void
test_queued_receive(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(m, "k", "v");
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.port0 = 40;
	out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out.port1 = 41;
	out.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	struct xpc_wire_held h;

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	host.destroy_port0 = 1;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_TIMED_OUT);
	CHECK(host.destroys == 1 && host.destroy_port0 == MACH_PORT_NULL);
	CHECK(ports_of(h.head)[0].name == 40);
	CHECK(ports_of(h.head)[0].disposition == MACH_MSG_TYPE_MOVE_RECEIVE);
	CHECK(ports_of(h.head)[1].disposition == MACH_MSG_TYPE_MAKE_SEND);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	host.send_result = KERN_SUCCESS;
	host.pseudo = false;
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	CHECK(ports_of(host.sent)[0].name == 40);
	CHECK(ports_of(host.sent)[0].disposition == MACH_MSG_TYPE_MOVE_RECEIVE);
	CHECK(ports_of(host.sent)[1].disposition == MACH_MSG_TYPE_MAKE_SEND);
	_xpc_wire_release(&h);
	CHECK(host.deallocs == 0);

	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	host.rename_receive = true;
	CHECK(_xpc_wire_try(&h) == (MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE));
	CHECK(host.destroys == 1 && host.destroy_port0 == 77);
	_xpc_wire_release(&h);

	reset();
	xpc_release(m);
}

static void
test_body_receive(void)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.port0 = 40;
	out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out.port1 = 41;
	out.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	struct xpc_wire_held h;
	static const kern_return_t kept[] = {
		MACH_SEND_INVALID_DEST, MACH_SEND_TOO_LARGE, MACH_SEND_INVALID_HEADER,
		MACH_SEND_INVALID_REPLY, MACH_SEND_INVALID_OPTIONS, MACH_SEND_MSG_FILTERED,
		MACH_SEND_TIMED_OUT, MACH_SEND_NO_BUFFER,
	};
	for (size_t i = 0; i < sizeof(kept) / sizeof(kept[0]); i++) {
		reset();
		CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
		CHECK(h.receive == 40);
		host.send_result = kept[i];
		CHECK(_xpc_wire_try(&h) == kept[i]);
		CHECK(h.receive == 40);
		_xpc_wire_release(&h);
		CHECK(host.receive_drops == 1 && host.receive_dropped == 40);
	}
	static const kern_return_t moved[] = {
		KERN_SUCCESS, MACH_SEND_INVALID_RIGHT, MACH_SEND_INVALID_MEMORY,
	};
	for (size_t i = 0; i < sizeof(moved) / sizeof(moved[0]); i++) {
		reset();
		CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
		host.send_result = moved[i];
		CHECK(_xpc_wire_try(&h) == moved[i]);
		CHECK(h.receive == MACH_PORT_NULL);
		_xpc_wire_release(&h);
		CHECK(host.receive_drops == 0);
	}
	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	host.send_result = MACH_SEND_TIMED_OUT;
	host.pseudo = true;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_TIMED_OUT && h.receive == 40);
	host.rename_receive = true;
	CHECK(_xpc_wire_try(&h) == (MACH_SEND_TIMED_OUT | MACH_MSG_IPC_SPACE));
	CHECK(h.receive == MACH_PORT_NULL);
	_xpc_wire_release(&h);
	CHECK(host.receive_drops == 0);

	reset();
	xpc_object_t bad = xpc_string_create("k");
	CHECK(_xpc_wire_prepare(bad, &out, &h) == KERN_INVALID_ARGUMENT && h.receive == 40);
	_xpc_wire_release(&h);
	CHECK(host.receive_drops == 1 && host.receive_dropped == 40);
	xpc_release(bad);
	reset();
	xpc_release(m);
}

static void
test_address(void)
{
	xpc_object_t m = queued_message();
	struct xpc_wire_out out = request_to(5);
	struct xpc_wire_held h;
	reset();
	CHECK(_xpc_wire_prepare(m, &out, &h) == KERN_SUCCESS);
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS);
	CHECK(h.receive == MACH_PORT_NULL && h.fixed == 0 && ports_of(h.head)[0].name == 100);
	CHECK(((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 2);
	struct xpc_wire_out hs = message_to(6, MACH_MSG_TYPE_COPY_SEND);
	hs.port0 = 40;
	hs.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	hs.port1 = 41;
	hs.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	_xpc_wire_address(&h, &hs);
	CHECK(h.receive == 40);
	CHECK(h.head->msgh_remote_port == 6 && h.head->msgh_local_port == 9);
	CHECK(MACH_MSGH_BITS_REMOTE(h.head->msgh_bits) == MACH_MSG_TYPE_COPY_SEND);
	CHECK(MACH_MSGH_BITS_LOCAL(h.head->msgh_bits) == MACH_MSG_TYPE_MAKE_SEND_ONCE);
	CHECK(h.head->msgh_bits & MACH_MSGH_BITS_COMPLEX);
	CHECK(ports_of(h.head)[0].name == 40);
	CHECK(ports_of(h.head)[0].disposition == MACH_MSG_TYPE_MOVE_RECEIVE);
	CHECK(ports_of(h.head)[1].name == 41);
	CHECK(ports_of(h.head)[1].disposition == MACH_MSG_TYPE_MAKE_SEND);
	CHECK(ports_of(h.head)[2].name == 100 && h.fixed == 3);
	CHECK(((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 4);
	CHECK(ool_of(h.head) != NULL && ool_of(h.head)->address == h.ool);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS && h.receive == MACH_PORT_NULL);
	CHECK(host.sent->msgh_remote_port == 6 && ports_of(host.sent)[0].name == 40);
	_xpc_wire_release(&h);
	CHECK(host.receive_drops == 0);

	reset();
	CHECK(_xpc_wire_prepare(m, &hs, &h) == KERN_SUCCESS);
	CHECK(h.fixed == 3 && ((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 4);
	struct xpc_wire_out half = message_to(7, MACH_MSG_TYPE_COPY_SEND);
	half.port1 = 41;
	half.port1_type = MACH_MSG_TYPE_MAKE_SEND;
	_xpc_wire_address(&h, &half);
	CHECK(h.fixed == 2 && h.receive == MACH_PORT_NULL);
	CHECK(((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 3);
	CHECK(ports_of(h.head)[0].name == 41 && ports_of(h.head)[1].name == 100);
	_xpc_wire_address(&h, &out);
	CHECK(h.fixed == 0 && ((mach_msg_body_t *)(h.head + 1))->msgh_descriptor_count == 2);
	CHECK(_xpc_wire_try(&h) == KERN_SUCCESS);
	mach_msg_header_t *copy = copy_message(host.sent);
	pseudo_receive(copy);
	xpc_object_t got = NULL;
	mach_port_t p0, p1;
	CHECK(_xpc_wire_decode(copy, &got, &p0, &p1) == XPC_WIRE_OK);
	CHECK(got != NULL && xpc_equal(got, m) && p0 == MACH_PORT_NULL && p1 == MACH_PORT_NULL);
	xpc_release(got);
	free(copy);
	_xpc_wire_release(&h);

	reset();
	CHECK(_xpc_wire_prepare(m, &hs, &h) == KERN_SUCCESS);
	host.fail_addref = true;
	CHECK(_xpc_wire_hold(&h) == KERN_SUCCESS && h.ports[0] == MACH_PORT_NULL);
	CHECK(ports_of(h.head)[0].name == 40 && ports_of(h.head)[1].name == 41);
	CHECK(ports_of(h.head)[2].name == MACH_PORT_NULL);
	host.fail_addref = false;
	h.receive = MACH_PORT_NULL;
	_xpc_wire_release(&h);

	xpc_object_t one = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, 100);
	xpc_dictionary_set_value(one, "p", port);
	XO(port)->port = MACH_PORT_NULL;
	xpc_release(port);
	struct xpc_wire_out once = message_to(7, MACH_MSG_TYPE_MOVE_SEND_ONCE);
	once.id = XPC_WIRE_REPLY;
	reset();
	CHECK(_xpc_wire_prepare(one, &once, &h) == KERN_SUCCESS && ool_of(h.head) == NULL);
	host.send_result = MACH_SEND_NO_BUFFER;
	host.pseudo = true;
	CHECK(_xpc_wire_try(&h) == MACH_SEND_NO_BUFFER && host.destroys == 1);
	_xpc_wire_release(&h);
	reset();
	host.send_result = MACH_SEND_NO_BUFFER;
	CHECK(_xpc_wire_prepare(one, &once, &h) == KERN_SUCCESS);
	CHECK(_xpc_wire_try(&h) == MACH_SEND_NO_BUFFER && host.destroys == 0);
	_xpc_wire_release(&h);
	xpc_release(one);

	for (int bad = 0; bad < 3; bad++) {
		reset();
		CHECK(_xpc_wire_send(m, &hs) == KERN_SUCCESS);
		copy = copy_message(host.sent);
		pseudo_receive(copy);
		mach_msg_port_descriptor_t *pd = ports_of(copy);
		uint8_t *wp = (uint8_t *)(pd + 3) + sizeof(mach_msg_ool_descriptor_t);
		struct wire_hdr wh;
		memcpy(&wh, wp, sizeof(wh));
		CHECK(wh.fixed == 3 && wh.nports == 1);
		if (bad == 0) {
			wh.fixed = 5;
		} else if (bad == 1) {
			wh.fixed = 1;
		} else {
			pd[0].disposition = MACH_MSG_TYPE_PORT_SEND;
		}
		memcpy(wp, &wh, sizeof(wh));
		got = NULL;
		CHECK(_xpc_wire_decode(copy, &got, &p0, &p1) == XPC_WIRE_BAD && got == NULL);
		free(copy);
	}
	reset();
	xpc_release(m);
}

static void
test_header_only_failures(void)
{
	xpc_object_t m = queued_message();
	struct xpc_wire_out out = message_to(5, MACH_MSG_TYPE_COPY_SEND);
	out.port0 = 30;
	out.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE;
	out.port1 = 31;
	out.port1_type = MACH_MSG_TYPE_MOVE_SEND;
	static const kern_return_t results[] = {
		MACH_SEND_INVALID_REPLY, MACH_SEND_INVALID_OPTIONS, MACH_SEND_MSG_FILTERED,
	};
	for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		reset();
		host.send_result = results[i];
		CHECK(_xpc_wire_send(m, &out) == results[i]);
		CHECK(host.destroys == 1 && host.destroy_bits == MACH_MSGH_BITS_COMPLEX);
		CHECK(host.vm_deallocs == 0);
		CHECK(!_xpc_wire_kept(results[i]));
	}
	reset();
	host.send_result = MACH_SEND_INVALID_RIGHT;
	CHECK(_xpc_wire_send(m, &out) == MACH_SEND_INVALID_RIGHT);
	CHECK(host.destroys == 0);
	xpc_release(m);
}

int
main(void)
{
	test_send_timeout();
	test_round_trip();
	test_duplicate_keys();
	test_many_keys();
	test_sizes();
	test_large_receive();
	test_oversize_traps();
	test_left_out();
	test_size_after_left_out();
	test_length_saturates();
	test_shared_and_cyclic();
	test_last_levels();
	test_cycle_members();
	test_height_limits();
	test_scan();
	test_decoder_checks();
	test_pass_allocations();
	test_scan_order();
	test_scan_allocations();
	test_truncated();
	test_fd_round_trip();
	test_memo_spread();
	test_ports_refused();
	test_connect_messages();
	test_header_wraps();
	test_connect_with_port();
	test_reply_ports();
	test_objects();
	test_failed_sends();
	test_returned_messages();
	test_queued_messages();
	test_queued_receive();
	test_header_only_failures();
	test_body_receive();
	test_address();
	reset();
	if (failures != 0) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("wire_test: all passed\n");
	return 0;
}
