/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include "../connection.c"

#include <mach/mig.h>

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__aarch64__) || defined(__arm64__)
#define TRAP_SIGNAL SIGTRAP
#else
#define TRAP_SIGNAL SIGILL
#endif

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

void
os_unfair_lock_lock(os_unfair_lock *lock)
{
	if (lock->owned) {
		fprintf(stderr, "lock taken twice\n");
		abort();
	}
	lock->owned = 1;
}

static void (*on_unlock)(os_unfair_lock *lock);

void
os_unfair_lock_unlock(os_unfair_lock *lock)
{
	if (!lock->owned) {
		fprintf(stderr, "lock released twice\n");
		abort();
	}
	lock->owned = 0;
	if (on_unlock != NULL) {
		on_unlock(lock);
	}
}

struct kmsg {
	struct kmsg *next;
	mach_msg_header_t *m;
	pid_t pid;
	struct kport *reply;
	void *ool;
};

struct kport {
	bool ours;
	bool alive;
	mach_port_t recv_name;
	struct kmsg *head, **tail;
	unsigned count, qlimit;
	bool rq;
	bool sp_armed;
	struct kport *rq_notify;
	mach_port_t rq_name;
	bool ns_armed;
	struct kport *ns_notify;
	int refill;
	mach_msg_header_t *refill_msg;
	unsigned received;
};

struct entry {
	struct kport *port;
	mach_port_t name;
	bool recv;
	unsigned send;
	bool once;
	bool dead;
	bool reply_port;
};

#define NNAMES 65536

static struct entry names[NNAMES];
static uint8_t gens[NNAMES];
static mach_port_t next_index = 1;
static mach_port_t free_index[NNAMES];
static int nfree;

#define NAME(n) names[MACH_PORT_INDEX(n)]

static struct {
	int errors;
	long vm_live;
	int receives;
	int max_batch;
	int timeouts;
	int handled;
	int invalid;
	int handled_marked;
	int hostile_handled;
	struct kport *watch, *busy;
	unsigned busy_at_watch;
	mach_msg_header_t *late;
	bool hold;
	mach_port_t listener;
	xpc_connection_t peers[1024];
	bool reject;
	bool reject_suspended;
	int replies;
	int reply_errors;
	int npeers;
	struct kport *order_port;
	bool order_ok;
	struct kport *service;
	mach_port_t service_name;
	int interrupted;
	int client_invalid;
	struct kport *sync_remote;
	int sync_next;
	bool sync_ordered;
	void (*blocked)(void);
	void (*timed_out)(void);
	void (*before_send)(void);
	void (*after_send)(void);
	void (*after_receive)(void);
	char events[64];
	int nevents;
	kern_return_t fail_send;
	kern_return_t fail_request;
	int lookups;
	uint64_t lookup_flags;
	int logged;
	int peer_interrupted;
	size_t fail_malloc;
	size_t fail_malloc_size;
	int fail_malloc_skip;
	int too_large;
	void (*during_lookup)(void);
	int malloc_failed;
	struct kport *body_error;
	unsigned sender_euid;
	int self_sends;
	bool wrap;
	void (*on_deallocate)(void);
	mach_port_t bad_right;
} host;

mach_port_t bootstrap_port = 1;

extern void *__libc_malloc(size_t size);

void *
malloc(size_t size)
{
	if ((host.fail_malloc != 0 && size >= host.fail_malloc) || size == host.fail_malloc_size) {
		if (host.fail_malloc_skip > 0) {
			host.fail_malloc_skip--;
			return __libc_malloc(size);
		}
		host.fail_malloc = host.fail_malloc_size = 0;
		host.malloc_failed++;
		return NULL;
	}
	return __libc_malloc(size);
}

void
test_os_log(void *log, const char *format, ...)
{
	(void)log;
	(void)format;
	host.logged++;
}

mach_port_t
mach_task_self(void)
{
	return 1;
}

static kern_return_t
bad(kern_return_t kr)
{
	host.errors++;
	return kr;
}

static struct entry *
entry_of(mach_port_t name)
{
	mach_port_t i = MACH_PORT_INDEX(name);
	if (!MACH_PORT_VALID(name) || i >= NNAMES || names[i].port == NULL || names[i].name != name) {
		return NULL;
	}
	return &names[i];
}

kern_return_t
mach_port_type(mach_port_t task, mach_port_name_t name, mach_port_type_t *type)
{
	(void)task;
	struct entry *e = entry_of(name);
	if (e == NULL) {
		return KERN_INVALID_NAME;
	}
	*type = e->recv ? MACH_PORT_TYPE_RECEIVE : 0;
	return KERN_SUCCESS;
}

static mach_port_t
index_name(mach_port_t i, struct kport *p, mach_port_t name)
{
	memset(&names[i], 0, sizeof(names[i]));
	names[i].port = p;
	names[i].name = name;
	return name;
}

static mach_port_t
name_new(struct kport *p)
{
	mach_port_t i = nfree > 0 ? free_index[--nfree] : next_index++;
	if (i >= NNAMES) {
		abort();
	}
	return index_name(i, p, (i << 8) | gens[i] | 3);
}

static mach_port_t
name_wrap(mach_port_t name, struct kport *p)
{
	mach_port_t i = MACH_PORT_INDEX(name);
	for (int k = 0; k < nfree; k++) {
		if (free_index[k] == i) {
			free_index[k] = free_index[--nfree];
			return index_name(i, p, name);
		}
	}
	abort();
}

static mach_port_t
once_new(struct kport *p)
{
	mach_port_t n = name_new(p);
	NAME(n).once = true;
	return n;
}

static void
name_gc(mach_port_t n)
{
	struct entry *e = &NAME(n);
	if (e->port != NULL && !e->recv && e->send == 0 && !e->once) {
		e->port = NULL;
		if (!host.wrap) {
			gens[MACH_PORT_INDEX(n)] += 4;
		}
		free_index[nfree++] = MACH_PORT_INDEX(n);
	}
}

static struct kport *
port_new(bool ours, unsigned qlimit)
{
	struct kport *p = calloc(1, sizeof(*p));
	p->ours = ours;
	p->alive = true;
	p->tail = &p->head;
	p->qlimit = qlimit;
	return p;
}

static void *
vm_new(const void *data, size_t size)
{
	void *r = malloc(size ? size : 1);
	memcpy(r, data, size);
	host.vm_live++;
	return r;
}

kern_return_t
vm_deallocate(mach_port_t task, vm_address_t address, vm_size_t size)
{
	(void)task;
	(void)size;
	free((void *)address);
	host.vm_live--;
	return KERN_SUCCESS;
}

static void
kq_put(struct kport *p, mach_msg_header_t *m, pid_t pid, struct kport *reply, void *ool)
{
	struct kmsg *k = calloc(1, sizeof(*k));
	k->m = m;
	k->pid = pid;
	k->reply = reply;
	k->ool = ool;
	*p->tail = k;
	p->tail = &k->next;
	p->count++;
}

static struct kmsg *
kq_take(struct kport *p)
{
	struct kmsg *k = p->head;
	if (k == NULL) {
		return NULL;
	}
	p->head = k->next;
	if (p->head == NULL) {
		p->tail = &p->head;
	}
	p->count--;
	return k;
}

static void
notify(struct kport *target, mach_msg_id_t id, mach_port_t not_port, pid_t pid)
{
	if (target == NULL || !target->alive) {
		return;
	}
	mach_dead_name_notification_t *n = calloc(1, sizeof(*n));
	n->not_header.msgh_bits = MACH_MSGH_BITS_SET(0, MACH_MSG_TYPE_PORT_SEND_ONCE, 0, 0);
	n->not_header.msgh_size = offsetof(mach_dead_name_notification_t, trailer);
	n->not_header.msgh_local_port = target->recv_name;
	n->not_header.msgh_id = id;
	n->not_port = not_port;
	kq_put(target, &n->not_header, pid, NULL, NULL);
}

static mach_msg_ool_descriptor_t *
ool_in(mach_msg_header_t *m)
{
	if (!(m->msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
		return NULL;
	}
	mach_msg_body_t *body = (mach_msg_body_t *)(m + 1);
	uint8_t *p = (uint8_t *)(body + 1);
	for (mach_msg_size_t i = 0; i < body->msgh_descriptor_count; i++) {
		mach_msg_type_descriptor_t *td = (mach_msg_type_descriptor_t *)p;
		if (td->type == MACH_MSG_OOL_DESCRIPTOR) {
			return (mach_msg_ool_descriptor_t *)p;
		}
		p += sizeof(mach_msg_port_descriptor_t);
	}
	return NULL;
}

static mach_msg_port_descriptor_t *
ports_in(mach_msg_header_t *m, mach_msg_size_t *n)
{
	mach_msg_body_t *body = (mach_msg_body_t *)(m + 1);
	mach_msg_port_descriptor_t *pd = (mach_msg_port_descriptor_t *)(body + 1);
	*n = 0;
	if (m->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		while (*n < body->msgh_descriptor_count && pd[*n].type == MACH_MSG_PORT_DESCRIPTOR) {
			(*n)++;
		}
	}
	return pd;
}

static void
kmsg_free(struct kmsg *k)
{
	mach_msg_ool_descriptor_t *od = ool_in(k->m);
	if (k->ool != NULL) {
		free(k->ool);
	} else if (od != NULL && od->size != 0) {
		vm_deallocate(1, (vm_address_t)od->address, od->size);
	}
	if (k->reply != NULL) {
		notify(k->reply, MACH_NOTIFY_SEND_ONCE, MACH_PORT_NULL, 0);
	}
	free(k->m);
	free(k);
}

static bool
watching_death(struct kport *p)
{
	return p->rq && !p->sp_armed;
}

static void
dead_notify(struct kport *target, mach_port_t name)
{
	if (entry_of(name) != NULL) {
		NAME(name).send++;
	}
	notify(target, MACH_NOTIFY_DEAD_NAME, name, 0);
}

static void
port_destroy(struct kport *p)
{
	p->alive = false;
	struct kmsg *k;
	while ((k = kq_take(p)) != NULL) {
		kmsg_free(k);
	}
	free(p->refill_msg);
	p->refill_msg = NULL;
	for (mach_port_t i = 1; i < next_index; i++) {
		struct entry *e = &names[i];
		if (e->port != p || e->recv) {
			continue;
		}
		if (e->once) {
			e->once = false;
			e->send++;
		}
		if (e->send > 0) {
			e->dead = true;
		}
	}
	if (p->rq) {
		p->rq = p->sp_armed = false;
		dead_notify(p->rq_notify, p->rq_name);
	}
}

kern_return_t
mach_port_deallocate(mach_port_t task, mach_port_name_t name)
{
	(void)task;
	if (host.on_deallocate != NULL) {
		void (*hook)(void) = host.on_deallocate;
		host.on_deallocate = NULL;
		hook();
	}
	struct entry *e = entry_of(name);
	if (e == NULL) {
		return bad(KERN_INVALID_NAME);
	}
	if (e->once) {
		struct kport *p = e->port;
		e->once = false;
		name_gc(name);
		notify(p, MACH_NOTIFY_SEND_ONCE, MACH_PORT_NULL, 0);
		return KERN_SUCCESS;
	}
	if (e->recv && e->send == 0) {
		return bad(KERN_INVALID_RIGHT);
	}
	if (e->send == 0) {
		return bad(KERN_INVALID_RIGHT);
	}
	e->send--;
	name_gc(name);
	return KERN_SUCCESS;
}

kern_return_t
mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right,
    mach_port_delta_t delta)
{
	(void)task;
	struct entry *e = entry_of(name);
	if (e == NULL) {
		return bad(KERN_INVALID_NAME);
	}
	if (right == MACH_PORT_RIGHT_SEND) {
		if (e->dead) {
			return KERN_INVALID_RIGHT;
		}
		if (delta < 0 && e->send < (unsigned)-delta) {
			return bad(KERN_INVALID_RIGHT);
		}
		e->send += (unsigned)delta;
		name_gc(name);
		return KERN_SUCCESS;
	}
	if (right == MACH_PORT_RIGHT_RECEIVE && delta == -1 && e->recv) {
		e->recv = false;
		port_destroy(e->port);
		name_gc(name);
		return KERN_SUCCESS;
	}
	return bad(KERN_INVALID_RIGHT);
}

kern_return_t
mach_port_insert_right(mach_port_t task, mach_port_name_t name, mach_port_t poly,
    mach_msg_type_name_t poly_poly)
{
	(void)task;
	struct entry *e = entry_of(name);
	if (e == NULL || name != poly || poly_poly != MACH_MSG_TYPE_MAKE_SEND || !e->recv) {
		return bad(KERN_INVALID_RIGHT);
	}
	e->send++;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_extract_right(mach_port_t task, mach_port_name_t name, mach_msg_type_name_t desired,
    mach_port_t *right, mach_msg_type_name_t *acquired)
{
	(void)task;
	struct entry *e = entry_of(name);
	if (e != NULL && e->reply_port) {
		return bad(KERN_INVALID_RIGHT);
	}
	if (e == NULL || !e->recv || desired != MACH_MSG_TYPE_MAKE_SEND_ONCE) {
		return bad(KERN_INVALID_RIGHT);
	}
	*right = once_new(e->port);
	*acquired = MACH_MSG_TYPE_MOVE_SEND_ONCE;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_construct(mach_port_t task, mach_port_options_t *options, uint64_t context,
    mach_port_name_t *name)
{
	(void)task;
	(void)context;
	struct kport *p = port_new(true, (options->flags & MPO_QLIMIT) ? options->mpl.mpl_qlimit : 5);
	mach_port_t n = name_new(p);
	NAME(n).recv = true;
	p->recv_name = n;
	if (options->flags & MPO_INSERT_SEND_RIGHT) {
		NAME(n).send = 1;
	}
	*name = n;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_request_notification(mach_port_t task, mach_port_name_t name, mach_msg_id_t msgid,
    mach_port_mscount_t sync, mach_port_t notify_port, mach_msg_type_name_t notify_poly,
    mach_port_t *previous)
{
	(void)task;
	*previous = MACH_PORT_NULL;
	struct entry *e = entry_of(name);
	if (e == NULL) {
		return KERN_INVALID_NAME;
	}
	struct kport *p = e->port;
	struct entry *t = entry_of(notify_port);
	if (host.fail_request != KERN_SUCCESS && t != NULL) {
		kern_return_t kr = host.fail_request;
		host.fail_request = KERN_SUCCESS;
		return kr;
	}
	if (MACH_PORT_VALID(notify_port) &&
	    (t == NULL || !t->recv || notify_poly != MACH_MSG_TYPE_MAKE_SEND_ONCE)) {
		return bad(KERN_INVALID_RIGHT);
	}
	if (msgid == MACH_NOTIFY_NO_SENDERS) {
		if (!e->recv) {
			return bad(KERN_INVALID_RIGHT);
		}
		p->ns_armed = t != NULL;
		p->ns_notify = t != NULL ? t->port : NULL;
		return KERN_SUCCESS;
	}
	if (msgid != MACH_NOTIFY_SEND_POSSIBLE && msgid != MACH_NOTIFY_DEAD_NAME) {
		return bad(KERN_INVALID_ARGUMENT);
	}
	if (e->dead) {
		if (msgid == MACH_NOTIFY_SEND_POSSIBLE && t != NULL) {
			dead_notify(t->port, name);
			return KERN_SUCCESS;
		}
		return KERN_INVALID_ARGUMENT;
	}
	if (p->rq) {
		*previous = once_new(p->rq_notify);
		p->rq = p->sp_armed = false;
	}
	if (t == NULL) {
		return KERN_SUCCESS;
	}
	if (msgid == MACH_NOTIFY_SEND_POSSIBLE && sync && p->count < p->qlimit) {
		notify(t->port, MACH_NOTIFY_SEND_POSSIBLE, name, 0);
		return KERN_SUCCESS;
	}
	p->rq = true;
	p->sp_armed = msgid == MACH_NOTIFY_SEND_POSSIBLE && sync;
	p->rq_notify = t->port;
	p->rq_name = name;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_set_attributes(mach_port_t task, mach_port_name_t name, mach_port_flavor_t flavor,
    mach_port_info_t info, mach_msg_type_number_t count)
{
	(void)task;
	(void)count;
	struct entry *e = entry_of(name);
	if (e == NULL || !e->recv || flavor != MACH_PORT_LIMITS_INFO) {
		return bad(KERN_INVALID_ARGUMENT);
	}
	e->port->qlimit = ((mach_port_limits_t *)info)->mpl_qlimit;
	return KERN_SUCCESS;
}

kern_return_t
mach_port_get_attributes(mach_port_t task, mach_port_name_t name, mach_port_flavor_t flavor,
    mach_port_info_t info, mach_msg_type_number_t *count)
{
	(void)task;
	struct entry *e = entry_of(name);
	if (e == NULL || !e->recv || flavor != MACH_PORT_RECEIVE_STATUS) {
		return bad(KERN_INVALID_ARGUMENT);
	}
	mach_port_status_t *st = (mach_port_status_t *)info;
	memset(st, 0, sizeof(*st));
	st->mps_qlimit = e->port->qlimit;
	st->mps_msgcount = e->port->count;
	*count = MACH_PORT_RECEIVE_STATUS_COUNT;
	return KERN_SUCCESS;
}

static mach_port_t mig_port;

mach_port_t
mig_get_reply_port(void)
{
	if (!MACH_PORT_VALID(mig_port)) {
		mach_port_options_t opts = { .flags = 0 };
		mach_port_construct(1, &opts, 0, &mig_port);
		NAME(mig_port).reply_port = true;
	}
	return mig_port;
}

void
mig_dealloc_reply_port(mach_port_t port)
{
	mach_port_mod_refs(1, port, MACH_PORT_RIGHT_RECEIVE, -1);
	mig_port = MACH_PORT_NULL;
}

kern_return_t
bootstrap_check_in(mach_port_t bp, const char *service_name, mach_port_t *sp)
{
	(void)bp;
	(void)service_name;
	mach_port_options_t opts = { .flags = MPO_QLIMIT, .mpl = { .mpl_qlimit = 1024 } };
	mach_port_construct(1, &opts, 0, sp);
	host.listener = *sp;
	return KERN_SUCCESS;
}

kern_return_t
bootstrap_look_up2(mach_port_t bp, const char *service_name, mach_port_t *sp, pid_t target_pid,
    uint64_t flags)
{
	(void)bp;
	(void)service_name;
	(void)target_pid;
	host.lookup_flags = flags;
	if (host.during_lookup != NULL) {
		void (*hook)(void) = host.during_lookup;
		host.during_lookup = NULL;
		hook();
	}
	if (host.service == NULL || !host.service->alive) {
		return KERN_FAILURE;
	}
	struct entry *e = entry_of(host.service_name);
	if (e == NULL || e->port != host.service) {
		host.service_name = name_new(host.service);
	}
	NAME(host.service_name).send++;
	*sp = host.service_name;
	host.lookups++;
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

static void
release_right(mach_port_t name, mach_msg_type_name_t type)
{
	if (!MACH_PORT_VALID(name)) {
		return;
	}
	switch (type) {
	case MACH_MSG_TYPE_MOVE_SEND:
	case MACH_MSG_TYPE_MOVE_SEND_ONCE:
		mach_port_deallocate(1, name);
		break;
	case MACH_MSG_TYPE_MOVE_RECEIVE:
		mach_port_mod_refs(1, name, MACH_PORT_RIGHT_RECEIVE, -1);
		break;
	default:
		break;
	}
}

void
mach_msg_destroy(mach_msg_header_t *msg)
{
	release_right(msg->msgh_remote_port, MACH_MSGH_BITS_REMOTE(msg->msgh_bits));
	mach_msg_size_t n;
	mach_msg_port_descriptor_t *pd = ports_in(msg, &n);
	for (mach_msg_size_t i = 0; i < n; i++) {
		release_right(pd[i].name, pd[i].disposition);
	}
	mach_msg_ool_descriptor_t *od = ool_in(msg);
	if (od != NULL && od->size != 0) {
		vm_deallocate(1, (vm_address_t)od->address, od->size);
	}
}

static bool
right_ok(mach_port_t name, mach_msg_type_name_t type)
{
	struct entry *e = entry_of(name);
	if (e == NULL) {
		return false;
	}
	switch (type) {
	case MACH_MSG_TYPE_COPY_SEND:
	case MACH_MSG_TYPE_MOVE_SEND:
		return e->send > 0;
	case MACH_MSG_TYPE_MOVE_SEND_ONCE:
		return e->once;
	case MACH_MSG_TYPE_MAKE_SEND:
	case MACH_MSG_TYPE_MAKE_SEND_ONCE:
	case MACH_MSG_TYPE_MOVE_RECEIVE:
		return e->recv;
	default:
		return false;
	}
}

static void
take_right(mach_port_t name, mach_msg_type_name_t type)
{
	struct entry *e = entry_of(name);
	switch (type) {
	case MACH_MSG_TYPE_MOVE_SEND:
		e->send--;
		break;
	case MACH_MSG_TYPE_MOVE_SEND_ONCE:
		e->once = false;
		break;
	case MACH_MSG_TYPE_MOVE_RECEIVE:
		e->recv = false;
		break;
	default:
		return;
	}
	name_gc(name);
}

static mach_msg_type_name_t
give_back(mach_port_t *name, mach_msg_type_name_t type, struct kport *port)
{
	struct entry *e = entry_of(*name);
	switch (type) {
	case MACH_MSG_TYPE_COPY_SEND:
	case MACH_MSG_TYPE_MOVE_SEND:
	case MACH_MSG_TYPE_MAKE_SEND:
		if (e == NULL || e->port != port) {
			*name = name_new(port);
			e = &NAME(*name);
			e->dead = !port->alive;
		}
		e->send++;
		return MACH_MSG_TYPE_MOVE_SEND;
	case MACH_MSG_TYPE_MOVE_SEND_ONCE:
	case MACH_MSG_TYPE_MAKE_SEND_ONCE:
		*name = once_new(port);
		return MACH_MSG_TYPE_MOVE_SEND_ONCE;
	case MACH_MSG_TYPE_MOVE_RECEIVE:
		if (e == NULL || e->port != port) {
			*name = name_new(port);
			e = &NAME(*name);
		}
		e->recv = true;
		port->recv_name = *name;
		return MACH_MSG_TYPE_MOVE_RECEIVE;
	default:
		abort();
	}
}

static mach_msg_return_t
send_cleaned(struct kport *reply, struct kport **carried, mach_msg_port_descriptor_t *pd,
    mach_msg_size_t n, mach_msg_return_t kr)
{
	if (reply != NULL) {
		notify(reply, MACH_NOTIFY_SEND_ONCE, MACH_PORT_NULL, 0);
	}
	for (mach_msg_size_t i = 0; i < n; i++) {
		if (carried[i] != NULL && pd[i].disposition == MACH_MSG_TYPE_MOVE_RECEIVE) {
			port_destroy(carried[i]);
		}
	}
	return kr;
}

static mach_msg_return_t
kernel_send(mach_msg_header_t *m, mach_msg_option_t option)
{
	if (host.before_send != NULL) {
		void (*hook)(void) = host.before_send;
		host.before_send = NULL;
		hook();
	}
	kern_return_t fail = host.fail_send;
	host.fail_send = KERN_SUCCESS;
	bool cleaned = fail == MACH_SEND_INVALID_OPTIONS || fail == MACH_SEND_MSG_FILTERED ||
	    fail == MACH_SEND_INVALID_RIGHT;
	if (fail != KERN_SUCCESS && !cleaned) {
		return fail;
	}
	if (m->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		mach_msg_size_t ports;
		ports_in(m, &ports);
		if (ports > 16383) {
			host.too_large++;
			return MACH_SEND_TOO_LARGE;
		}
	}
	struct entry *d = entry_of(m->msgh_remote_port);
	mach_msg_type_name_t rt = MACH_MSGH_BITS_REMOTE(m->msgh_bits);
	mach_msg_type_name_t lt = MACH_MSGH_BITS_LOCAL(m->msgh_bits);
	if (d == NULL || d->dead || !d->port->alive || !right_ok(m->msgh_remote_port, rt)) {
		return MACH_SEND_INVALID_DEST;
	}
	if (MACH_PORT_VALID(m->msgh_local_port) && !right_ok(m->msgh_local_port, lt)) {
		return MACH_SEND_INVALID_REPLY;
	}
	struct kport *p = d->port;
	struct kport *reply = MACH_PORT_VALID(m->msgh_local_port) ?
	    entry_of(m->msgh_local_port)->port : NULL;
	if (p->ours && m->msgh_id != XPC_WIRE_WAKE) {
		host.self_sends++;
	}
	take_right(m->msgh_remote_port, rt);
	if (reply != NULL) {
		take_right(m->msgh_local_port, lt);
	}
	mach_msg_size_t n;
	mach_msg_port_descriptor_t *pd = ports_in(m, &n);
	struct kport *carried[n ? n : 1];
	memset(carried, 0, sizeof(carried));
	if (fail != KERN_SUCCESS && fail != MACH_SEND_INVALID_RIGHT) {
		return send_cleaned(reply, carried, pd, n, fail);
	}
	for (mach_msg_size_t i = 0; i < n; i++) {
		if (!MACH_PORT_VALID(pd[i].name)) {
			continue;
		}
		if (!right_ok(pd[i].name, pd[i].disposition) || pd[i].name == host.bad_right) {
			return send_cleaned(reply, carried, pd, n, MACH_SEND_INVALID_RIGHT);
		}
		carried[i] = entry_of(pd[i].name)->port;
		take_right(pd[i].name, pd[i].disposition);
	}
	if (fail != KERN_SUCCESS) {
		return send_cleaned(reply, carried, pd, n, fail);
	}
	void *ool = NULL;
	mach_msg_ool_descriptor_t *od = ool_in(m);
	if (od != NULL) {
		ool = malloc(od->size ? od->size : 1);
		memcpy(ool, od->address, od->size);
		if (od->deallocate) {
			host.errors++;
		}
	}
	if (p->count >= p->qlimit && rt != MACH_MSG_TYPE_MOVE_SEND_ONCE &&
	    rt != MACH_MSG_TYPE_MAKE_SEND_ONCE) {
		if (!(option & MACH_SEND_TIMEOUT)) {
			abort();
		}
		rt = give_back(&m->msgh_remote_port, rt, p);
		if (reply != NULL && !(lt == MACH_MSG_TYPE_MAKE_SEND_ONCE &&
		    entry_of(m->msgh_local_port)->reply_port)) {
			lt = give_back(&m->msgh_local_port, lt, reply);
		}
		m->msgh_bits = MACH_MSGH_BITS_SET(rt, reply != NULL ? lt : 0, 0,
		    m->msgh_bits & MACH_MSGH_BITS_COMPLEX);
		for (mach_msg_size_t i = 0; i < n; i++) {
			if (carried[i] != NULL) {
				pd[i].disposition = give_back(&pd[i].name, pd[i].disposition, carried[i]);
			} else if (pd[i].disposition == MACH_MSG_TYPE_COPY_SEND) {
				pd[i].disposition = MACH_MSG_TYPE_MOVE_SEND;
			}
		}
		if (od != NULL) {
			od->address = vm_new(ool, od->size);
			od->deallocate = true;
			free(ool);
		}
		host.timeouts++;
		if (host.timed_out != NULL) {
			void (*hook)(void) = host.timed_out;
			host.timed_out = NULL;
			hook();
		}
		return MACH_SEND_TIMED_OUT;
	}
	for (mach_msg_size_t i = 0; i < n; i++) {
		if (carried[i] != NULL && pd[i].disposition == MACH_MSG_TYPE_MOVE_RECEIVE) {
			carried[i]->ours = false;
		}
	}
	mach_msg_header_t *copy = malloc(m->msgh_size);
	memcpy(copy, m, m->msgh_size);
	kq_put(p, copy, getpid(), reply, ool);
	if (host.after_send != NULL) {
		void (*hook)(void) = host.after_send;
		host.after_send = NULL;
		hook();
	}
	return KERN_SUCCESS;
}

static mach_msg_return_t
kernel_receive(mach_msg_header_t *msg, mach_msg_option_t option, mach_msg_size_t rcv_size,
    mach_port_name_t rcv_name)
{
	struct entry *e = entry_of(rcv_name);
	if (e == NULL || !e->recv) {
		return MACH_RCV_INVALID_NAME;
	}
	struct kport *p = e->port;
	for (int i = 0; p->head == NULL && !(option & MACH_RCV_TIMEOUT) && host.blocked != NULL &&
	    i < 100000; i++) {
		host.blocked();
	}
	if (p->head == NULL) {
		if (!(option & MACH_RCV_TIMEOUT)) {
			abort();
		}
		return MACH_RCV_TIMED_OUT;
	}
	mach_msg_header_t *m = p->head->m;
	mach_msg_size_t need = round_msg(m->msgh_size) + sizeof(mach_msg_audit_trailer_t);
	if (need > rcv_size) {
		msg->msgh_size = m->msgh_size;
		if (option & MACH_RCV_LARGE) {
			return MACH_RCV_TOO_LARGE;
		}
		kmsg_free(kq_take(p));
		return MACH_RCV_TOO_LARGE;
	}
	struct kmsg *k = kq_take(p);
	memcpy(msg, m, m->msgh_size);
	mach_msg_audit_trailer_t *t =
	    (mach_msg_audit_trailer_t *)((uint8_t *)msg + round_msg(msg->msgh_size));
	memset(t, 0, sizeof(*t));
	t->msgh_trailer_size = sizeof(*t);
	t->msgh_audit.val[5] = (unsigned int)k->pid;
	t->msgh_audit.val[1] = k->pid != 0 ? host.sender_euid : 0;
	free(k->m);
	free(k);
	host.receives++;
	p->received++;
	if (p == host.body_error) {
		host.body_error = NULL;
		return MACH_RCV_BODY_ERROR | MACH_MSG_VM_SPACE;
	}
	if (host.after_receive != NULL) {
		void (*hook)(void) = host.after_receive;
		host.after_receive = NULL;
		hook();
	}
	if (p == host.busy && p->received == 5 && host.late != NULL) {
		kq_put(host.watch, host.late, 1234, NULL, NULL);
		host.late = NULL;
	}
	if (p == host.watch && host.busy != NULL) {
		host.busy_at_watch = host.busy->received;
	}
	if (p->refill > 0 && p->count < p->qlimit && p->refill_msg != NULL) {
		p->refill--;
		mach_msg_header_t *r = malloc(p->refill_msg->msgh_size);
		memcpy(r, p->refill_msg, p->refill_msg->msgh_size);
		kq_put(p, r, 4321, NULL, NULL);
	}
	return KERN_SUCCESS;
}

mach_msg_return_t
mach_msg(mach_msg_header_t *msg, mach_msg_option_t option, mach_msg_size_t send_size,
    mach_msg_size_t rcv_size, mach_port_name_t rcv_name, mach_msg_timeout_t timeout,
    mach_port_name_t notify_port)
{
	(void)send_size;
	(void)timeout;
	(void)notify_port;
	if (option & MACH_SEND_MSG) {
		return kernel_send(msg, option);
	}
	return kernel_receive(msg, option, rcv_size, rcv_name);
}

struct ditem {
	struct ditem *next;
	dispatch_block_t block;
};

struct dispatch_queue_s {
	struct dispatch_queue_s *all;
	bool source;
	int refs;
	int susp;
	struct ditem *head, **tail;
	mach_port_t port;
	dispatch_queue_t target;
	dispatch_block_t event, cancel;
	bool cancelled, cancel_queued, pending;
};

static struct dispatch_queue_s *dispatch_all;

static struct dispatch_queue_s *
dobj_new(bool source)
{
	struct dispatch_queue_s *q = calloc(1, sizeof(*q));
	q->source = source;
	q->refs = 1;
	q->tail = &q->head;
	q->all = dispatch_all;
	dispatch_all = q;
	return q;
}

dispatch_queue_t
dispatch_queue_create(const char *label, const void *attr)
{
	(void)label;
	(void)attr;
	return dobj_new(false);
}

void
dispatch_retain(void *object)
{
	((struct dispatch_queue_s *)object)->refs++;
}

void
dispatch_release(void *object)
{
	struct dispatch_queue_s *q = object;
	if (q->refs <= 0) {
		abort();
	}
	q->refs--;
}

void
dispatch_suspend(void *object)
{
	((struct dispatch_queue_s *)object)->susp++;
}

void
dispatch_resume(void *object)
{
	struct dispatch_queue_s *q = object;
	if (q->susp <= 0) {
		fprintf(stderr, "dispatch object resumed too often\n");
		abort();
	}
	q->susp--;
}

void
dispatch_set_target_queue(void *object, dispatch_queue_t queue)
{
	((struct dispatch_queue_s *)object)->target = queue;
}

void
dispatch_async(dispatch_queue_t queue, dispatch_block_t block)
{
	struct ditem *it = calloc(1, sizeof(*it));
	it->block = Block_copy(block);
	*queue->tail = it;
	queue->tail = &it->next;
}

dispatch_source_t
dispatch_source_create(dispatch_source_type_t type, uintptr_t handle, uintptr_t mask,
    dispatch_queue_t queue)
{
	(void)type;
	(void)mask;
	struct dispatch_queue_s *s = dobj_new(true);
	s->susp = 1;
	s->port = (mach_port_t)handle;
	s->target = queue;
	return s;
}

void
dispatch_source_set_event_handler(dispatch_source_t source, dispatch_block_t handler)
{
	source->event = Block_copy(handler);
}

void
dispatch_source_set_cancel_handler(dispatch_source_t source, dispatch_block_t handler)
{
	source->cancel = Block_copy(handler);
}

void
dispatch_source_cancel(dispatch_source_t source)
{
	source->cancelled = true;
}

static bool
port_ready(mach_port_t name)
{
	struct entry *e = entry_of(name);
	return e != NULL && e->recv && e->port->head != NULL;
}

static bool
step(void)
{
	bool queued = false;
	for (struct dispatch_queue_s *s = dispatch_all; s != NULL; s = s->all) {
		if (!s->source || s->susp > 0 || s->pending) {
			continue;
		}
		if (!s->cancelled && port_ready(s->port)) {
			s->pending = true;
			dispatch_async(s->target, ^{
				int before = host.receives;
				s->event();
				if (host.receives - before > host.max_batch) {
					host.max_batch = host.receives - before;
				}
				s->pending = false;
			});
			queued = true;
		} else if (s->cancelled && !s->cancel_queued) {
			s->cancel_queued = true;
			dispatch_async(s->target, s->cancel);
			queued = true;
		}
	}
	for (struct dispatch_queue_s *q = dispatch_all; q != NULL; q = q->all) {
		if (q->source || q->susp > 0 || q->head == NULL) {
			continue;
		}
		struct ditem *it = q->head;
		q->head = it->next;
		if (q->head == NULL) {
			q->tail = &q->head;
		}
		it->block();
		Block_release(it->block);
		free(it);
		return true;
	}
	return queued;
}

static void
run(void)
{
	for (long i = 0; step(); i++) {
		if (i > 10000000) {
			fprintf(stderr, "run loop does not settle\n");
			abort();
		}
	}
}

static mach_msg_type_name_t
as_received(mach_msg_type_name_t type)
{
	switch (type) {
	case MACH_MSG_TYPE_COPY_SEND:
	case MACH_MSG_TYPE_MAKE_SEND:
		return MACH_MSG_TYPE_PORT_SEND;
	case MACH_MSG_TYPE_MAKE_SEND_ONCE:
		return MACH_MSG_TYPE_PORT_SEND_ONCE;
	default:
		return type;
	}
}

static mach_msg_header_t *
client_message(xpc_object_t dict, mach_port_t dest, mach_port_t port0, mach_port_t port1)
{
	struct xpc_wire_out out = {
		.dest = dest,
		.dest_type = MACH_MSG_TYPE_COPY_SEND,
		.port0 = port0,
		.port0_type = MACH_MSG_TYPE_MOVE_RECEIVE,
		.port1 = port1,
		.port1_type = MACH_MSG_TYPE_MOVE_SEND,
		.id = XPC_WIRE_MESSAGE,
	};
	struct xpc_wire_held h;
	if (_xpc_wire_prepare(dict, &out, &h) != KERN_SUCCESS) {
		abort();
	}
	mach_msg_header_t *m = malloc(h.head->msgh_size);
	memcpy(m, h.head, h.head->msgh_size);
	m->msgh_bits = MACH_MSGH_BITS_SET(0, MACH_MSG_TYPE_PORT_SEND, 0, MACH_MSGH_BITS_COMPLEX);
	m->msgh_remote_port = MACH_PORT_NULL;
	m->msgh_local_port = dest;
	mach_msg_size_t n;
	mach_msg_port_descriptor_t *pd = ports_in(m, &n);
	for (mach_msg_size_t i = 0; i < n; i++) {
		pd[i].disposition = as_received(pd[i].disposition);
	}
	mach_msg_ool_descriptor_t *od = ool_in(m);
	if (od != NULL) {
		od->address = vm_new(h.ool, od->size);
	}
	h.receive = MACH_PORT_NULL;
	_xpc_wire_release(&h);
	return m;
}

static unsigned
fixed_in(mach_msg_header_t *m)
{
	mach_msg_size_t n;
	uint8_t *p = (uint8_t *)(ports_in(m, &n) + n);
	if (ool_in(m) != NULL) {
		p += sizeof(mach_msg_ool_descriptor_t);
	}
	uint32_t wh[6];
	memcpy(wh, p, sizeof(wh));
	return (wh[5] & 1u) + ((wh[5] >> 1) & 1u);
}

static int
client_seq(struct kmsg *k)
{
	mach_msg_header_t *m = calloc(1, round_msg(k->m->msgh_size) + sizeof(mach_msg_max_trailer_t));
	memcpy(m, k->m, k->m->msgh_size);
	m->msgh_bits = MACH_MSGH_BITS_SET(0, MACH_MSG_TYPE_PORT_SEND, 0, MACH_MSGH_BITS_COMPLEX);
	m->msgh_remote_port = MACH_PORT_NULL;
	m->msgh_local_port = MACH_PORT_NULL;
	m->msgh_id = XPC_WIRE_MESSAGE;
	mach_msg_size_t n;
	mach_msg_port_descriptor_t *pd = ports_in(m, &n);
	static struct kport *elsewhere;
	if (elsewhere == NULL) {
		elsewhere = port_new(false, 1);
	}
	unsigned fixed = fixed_in(m);
	for (mach_msg_size_t i = 0; i < n; i++) {
		pd[i].disposition = as_received(pd[i].disposition);
		if (i >= fixed && MACH_PORT_VALID(pd[i].name)) {
			pd[i].name = name_new(elsewhere);
			NAME(pd[i].name).send = 1;
		} else {
			pd[i].name = MACH_PORT_NULL;
		}
	}
	mach_msg_ool_descriptor_t *od = ool_in(m);
	if (od != NULL) {
		od->address = vm_new(k->ool, od->size);
	}
	xpc_object_t dict = NULL;
	mach_port_t p0, p1;
	int seq = -1;
	if (_xpc_wire_decode(m, &dict, &p0, &p1) == XPC_WIRE_OK) {
		seq = (int)xpc_dictionary_get_int64(dict, "seq");
		xpc_release(dict);
	}
	free(m);
	return seq;
}

struct client {
	struct kport *remote;
	mach_port_t remote_name;
	struct kport *local;
	mach_port_t local_name;
	xpc_connection_t peer;
};

static int
client_answer(struct client *cl, bool answer)
{
	struct kport *p = cl->remote;
	struct kmsg *k = kq_take(p);
	if (k == NULL) {
		return -1;
	}
	int seq = client_seq(k);
	if (answer && k->reply != NULL && k->reply->alive) {
		xpc_object_t r = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_set_int64(r, "seq", seq);
		mach_msg_header_t *m = client_message(r, k->reply->recv_name, MACH_PORT_NULL,
		    MACH_PORT_NULL);
		m->msgh_id = XPC_WIRE_REPLY;
		kq_put(k->reply, m, 1234, NULL, NULL);
		xpc_release(r);
		k->reply = NULL;
	}
	kmsg_free(k);
	if (p->sp_armed) {
		p->rq = p->sp_armed = false;
		notify(p->rq_notify, MACH_NOTIFY_SEND_POSSIBLE, p->rq_name, 0);
	}
	return seq;
}

static int
client_take(struct client *cl)
{
	return client_answer(cl, false);
}

static void
client_die(struct client *cl)
{
	port_destroy(cl->remote);
	if (cl->local->alive && cl->local->ns_armed) {
		cl->local->ns_armed = false;
		notify(cl->local->ns_notify, MACH_NOTIFY_NO_SENDERS, 0, 0);
	}
}

static void
peer_event(xpc_object_t event)
{
	if (xpc_get_type(event) == XPC_TYPE_DICTIONARY) {
		if (host.order_port != NULL && host.order_port->received != (unsigned)host.handled) {
			host.order_ok = false;
		}
		host.handled++;
		if (xpc_dictionary_get_bool(event, "hostile")) {
			host.hostile_handled++;
		}
		if (xpc_dictionary_get_bool(event, "marked")) {
			host.handled_marked++;
		}
	} else if (event == XPC_ERROR_CONNECTION_INVALID) {
		host.invalid++;
	} else if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
		host.peer_interrupted++;
	}
}

static xpc_connection_t listener;
static int rejected;

static void
rejected_gone(void *context)
{
	(void)context;
	rejected++;
}

static void
start_listener(void)
{
	listener = xpc_connection_create_mach_service("svc", NULL,
	    XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
		if (xpc_get_type(event) != XPC_TYPE_CONNECTION) {
			return;
		}
		if (host.reject) {
			xpc_connection_set_finalizer_f(event, rejected_gone);
			if (host.reject_suspended) {
				xpc_connection_suspend(event);
				xpc_connection_suspend(event);
			}
			xpc_connection_cancel(event);
			return;
		}
		xpc_connection_t peer = (xpc_connection_t)xpc_retain(event);
		host.peers[host.npeers++] = peer;
		xpc_connection_set_event_handler(peer, ^(xpc_object_t e) {
			peer_event(e);
		});
		if (!host.hold) {
			xpc_connection_resume(peer);
		}
	});
	xpc_connection_resume(listener);
}

static xpc_object_t
seq_message(int seq, size_t data)
{
	xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_int64(m, "seq", seq);
	if (data != 0) {
		void *buf = calloc(1, data);
		xpc_dictionary_set_data(m, "data", buf, data);
		free(buf);
	}
	return m;
}

static struct client
connect_data(unsigned qlimit, size_t data)
{
	struct client cl = { 0 };
	mach_port_options_t opts = { .flags = MPO_QLIMIT, .mpl = { .mpl_qlimit = 5 } };
	mach_port_construct(1, &opts, 0, &cl.local_name);
	cl.local = NAME(cl.local_name).port;
	cl.remote = port_new(false, qlimit);
	cl.remote_name = name_new(cl.remote);
	NAME(cl.remote_name).send = 1;
	xpc_object_t first = seq_message(-1, data);
	kq_put(NAME(host.listener).port,
	    client_message(first, host.listener, cl.local_name, cl.remote_name), 1234, NULL, NULL);
	xpc_release(first);
	return cl;
}

static struct client
connect_begin(unsigned qlimit)
{
	return connect_data(qlimit, 0);
}

static struct client
connect_client(unsigned qlimit)
{
	int before = host.npeers;
	struct client cl = connect_begin(qlimit);
	run();
	if (host.npeers != before + 1) {
		abort();
	}
	cl.peer = host.peers[before];
	return cl;
}

static void
inject(struct client *cl, xpc_object_t dict)
{
	kq_put(cl->local, client_message(dict, cl->local_name, MACH_PORT_NULL, MACH_PORT_NULL), 1234,
	    NULL, NULL);
}

static void
send_seq(struct client *cl, int seq, size_t data)
{
	xpc_object_t m = seq_message(seq, data);
	xpc_connection_send_message(cl->peer, m);
	xpc_release(m);
}

static void
finish(struct client *cl)
{
	xpc_connection_cancel(cl->peer);
	run();
	CHECK(entry_of(cl->remote_name) == NULL);
	CHECK(entry_of(cl->local_name) == NULL);
}

static size_t
queued(xpc_connection_t peer)
{
	size_t n = 0;
	for (struct queued *q = CONN(peer)->sendq; q != NULL; q = q->next) {
		n++;
	}
	return n;
}

static void
test_note_buckets(void)
{
	struct note **seen[NOTE_BUCKETS];
	bool distinct = true;
	for (mach_port_t i = 0; i < NOTE_BUCKETS; i++) {
		seen[i] = note_find(((i + 1) << 8) | 3);
		for (mach_port_t j = 0; j < i; j++) {
			distinct = distinct && seen[j] != seen[i];
		}
	}
	CHECK(distinct);
}

static void
test_flow_control(void)
{
	struct client cl = connect_client(4);
	struct conn *c = CONN(cl.peer);
	for (int i = 0; i < 100; i++) {
		send_seq(&cl, i, i % 10 == 0 ? 20000 : 0);
	}
	CHECK(cl.remote->count == 4);
	CHECK(queued(cl.peer) == 96);
	CHECK(cl.remote->sp_armed);
	CHECK(NAME(cl.remote_name).send == 2);
	int next = 0, sent = 100;
	bool ordered = true;
	for (int guard = 0; guard < 1000 && next < 150; guard++) {
		while (cl.remote->count > 0) {
			int got = client_take(&cl);
			ordered = ordered && got == next;
			next++;
		}
		if (sent < 150) {
			send_seq(&cl, sent, 0);
			sent++;
		}
		run();
	}
	CHECK(next == 150 && ordered);
	CHECK(c->sendq == NULL && c->sendq_dest == MACH_PORT_NULL);
	CHECK(NAME(cl.remote_name).send == 1);
	CHECK(!c->cancelled && host.invalid == 0);
	send_seq(&cl, 150, 0);
	CHECK(cl.remote->count == 1 && c->sendq == NULL);
	CHECK(client_take(&cl) == 150);
	CHECK(host.vm_live == 0);
	finish(&cl);
	CHECK(host.invalid == 1);
}

static struct kport *
notify_kport(void)
{
	return NAME(notify_port).port;
}

static void
test_spoofed_notifications(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	for (int i = 0; i < 3; i++) {
		send_seq(&cl, i, 0);
	}
	CHECK(queued(cl.peer) == 2 && cl.remote->sp_armed);
	struct kport *np = notify_kport();
	CHECK(client_take(&cl) == 0);
	CHECK(np->count == 1 && cl.local->count == 0);
	struct kmsg *real = np->head;
	np->head = NULL;
	np->tail = &np->head;
	np->count = 0;
	notify(np, MACH_NOTIFY_SEND_POSSIBLE, cl.remote_name, 1234);
	notify(np, MACH_NOTIFY_DEAD_NAME, cl.remote_name, 1234);
	notify(np, MACH_NOTIFY_SEND_POSSIBLE, cl.local_name, 0);
	notify(np, MACH_NOTIFY_DEAD_NAME, cl.local_name, 0);
	notify(np, MACH_NOTIFY_DEAD_NAME, cl.remote_name, 0);
	np->head->next->next->next->next->m->msgh_bits |= MACH_MSGH_BITS_COMPLEX;
	notify(cl.local, MACH_NOTIFY_SEND_POSSIBLE, cl.remote_name, 0);
	notify(cl.local, MACH_NOTIFY_DEAD_NAME, cl.remote_name, 0);
	notify(cl.local, MACH_NOTIFY_NO_SENDERS, 0, 1234);
	run();
	CHECK(cl.remote->count == 0 && queued(cl.peer) == 2 && c->sp_armed);
	CHECK(!c->cancelled && host.invalid == 0);
	CHECK(NAME(cl.remote_name).send == 2);
	kq_put(np, real->m, 0, NULL, NULL);
	free(real);
	run();
	CHECK(cl.remote->count == 1 && queued(cl.peer) == 1);
	CHECK(client_take(&cl) == 1);
	run();
	CHECK(client_take(&cl) == 2);
	run();
	CHECK(c->sendq == NULL && !c->cancelled);
	struct note *n = *note_find(cl.remote_name);
	CHECK(n != NULL && n->requests == 0 && n->watchers == c);
	notify(np, MACH_NOTIFY_DEAD_NAME, cl.remote_name, 0);
	run();
	CHECK(!c->cancelled && host.invalid == 0 && NAME(cl.remote_name).send == 1);
	finish(&cl);
	CHECK(*note_find(cl.remote_name) == NULL);
}

static void
test_notify_while_held(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	int handled = host.handled;
	dispatch_suspend(c->q);
	xpc_object_t m = seq_message(100, 0);
	inject(&cl, m);
	xpc_release(m);
	run();
	CHECK(cl.local->count == 0 && host.handled == handled);
	for (int i = 0; i < 4; i++) {
		send_seq(&cl, i, 0);
	}
	CHECK(queued(cl.peer) == 3 && cl.remote->sp_armed);
	CHECK(client_take(&cl) == 0);
	run();
	CHECK(cl.remote->count == 1 && queued(cl.peer) == 2);
	CHECK(client_take(&cl) == 1);
	run();
	CHECK(cl.remote->count == 1 && queued(cl.peer) == 1);
	CHECK(host.handled == handled);
	client_die(&cl);
	run();
	CHECK(c->cancelled && host.invalid == 0 && host.handled == handled);
	CHECK(entry_of(cl.remote_name) == NULL);
	CHECK(*note_find(cl.remote_name) == NULL);
	dispatch_resume(c->q);
	run();
	CHECK(host.handled == handled + 1 && host.invalid == 1);
	CHECK(entry_of(cl.local_name) == NULL);
	CHECK(host.vm_live == 0);
}

static void
test_gone_notifications(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	for (int i = 0; i < 3; i++) {
		send_seq(&cl, i, 0);
	}
	CHECK(queued(cl.peer) == 2 && cl.remote->sp_armed);
	struct kport *np = notify_kport();
	CHECK(client_take(&cl) == 0);
	CHECK(np->count == 1);
	xpc_connection_cancel(cl.peer);
	struct note *n = *note_find(cl.remote_name);
	CHECK(n != NULL && n->watchers == NULL && n->requests == 1);
	run();
	CHECK(np->count == 0 && *note_find(cl.remote_name) == NULL);
	CHECK(entry_of(cl.remote_name) == NULL && entry_of(cl.local_name) == NULL);
	CHECK(host.invalid == 1);

	struct client d = connect_client(1);
	for (int i = 0; i < 3; i++) {
		send_seq(&d, i, 0);
	}
	CHECK(queued(d.peer) == 2 && d.remote->sp_armed);
	client_die(&d);
	CHECK(np->count == 1 && NAME(d.remote_name).send == 3);
	xpc_connection_cancel(d.peer);
	CHECK(NAME(d.remote_name).send == 1);
	n = *note_find(d.remote_name);
	CHECK(n != NULL && n->watchers == NULL && n->requests == 1);
	run();
	CHECK(np->count == 0 && *note_find(d.remote_name) == NULL);
	CHECK(entry_of(d.remote_name) == NULL && entry_of(d.local_name) == NULL);
	CHECK(host.invalid == 2);

	struct client e = connect_client(1);
	for (int i = 0; i < 3; i++) {
		send_seq(&e, i, 0);
	}
	CHECK(e.remote->sp_armed && *note_find(e.remote_name) != NULL);
	xpc_connection_cancel(e.peer);
	CHECK(!e.remote->sp_armed && *note_find(e.remote_name) == NULL);
	CHECK(np->count == 1);
	run();
	CHECK(np->count == 0 && entry_of(e.remote_name) == NULL && entry_of(e.local_name) == NULL);
	CHECK(host.invalid == 3);
	CHECK(host.vm_live == 0);
}

static void
test_full_client(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	const int total = 20000;
	size_t big = 0;
	for (int i = 0; i < total; i++) {
		size_t data = i == 3 ? (8u << 20) - 4096 : i % 250 == 1 ? (1u << 20) : 0;
		big += data;
		send_seq(&cl, i, data);
	}
	CHECK(big > (64u << 20));
	CHECK(queued(cl.peer) == (size_t)total - 1);
	CHECK(NAME(cl.remote_name).send == 2);
	CHECK(!c->cancelled && host.invalid == 0);
	int next = 0;
	bool ordered = true, urefs = true;
	for (int guard = 0; guard < 2 * total && next < total; guard++) {
		while (cl.remote->count > 0) {
			int got = client_take(&cl);
			ordered = ordered && got == next;
			next++;
		}
		run();
		urefs = urefs && NAME(cl.remote_name).send <= 2;
	}
	CHECK(next == total && ordered && urefs);
	CHECK(c->sendq == NULL && c->sendq_dest == MACH_PORT_NULL);
	CHECK(NAME(cl.remote_name).send == 1);
	CHECK(!c->cancelled && host.invalid == 0);
	CHECK(host.vm_live == 0);
	finish(&cl);
	CHECK(host.invalid == 1);
}

static void
test_one_at_a_time(void)
{
	host.hold = true;
	host.handled = 0;
	host.max_batch = 0;
	struct client cl = connect_client(4);
	for (int i = 0; i < 40; i++) {
		xpc_object_t m = seq_message(i, i % 8 == 0 ? 100000 : 0);
		inject(&cl, m);
		xpc_release(m);
	}
	run();
	CHECK(cl.local->count == 40 && cl.local->received == 0);
	CHECK(host.handled == 0);
	host.hold = false;
	host.order_port = cl.local;
	host.order_ok = true;
	xpc_connection_resume(cl.peer);
	run();
	CHECK(host.handled == 41 && host.order_ok);
	CHECK(cl.local->count == 0 && host.max_batch == 1);
	host.order_port = NULL;
	CHECK(host.vm_live == 0);
	finish(&cl);

	int before = host.npeers;
	xpc_connection_suspend(listener);
	struct client waiting[3];
	for (int i = 0; i < 3; i++) {
		waiting[i] = connect_begin(4);
	}
	run();
	CHECK(host.npeers == before);
	CHECK(NAME(host.listener).port->count == 2);
	xpc_connection_resume(listener);
	run();
	CHECK(host.npeers == before + 3);
	CHECK(NAME(host.listener).port->count == 0);
	for (int i = 0; i < 3; i++) {
		waiting[i].peer = host.peers[before + i];
		finish(&waiting[i]);
	}
}

static void
test_busy_client(void)
{
	host.handled = 0;
	host.hostile_handled = 0;
	host.handled_marked = 0;
	host.max_batch = 0;
	struct client bad_cl = connect_client(4);
	struct client good = connect_client(4);
	xpc_object_t hostile = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_bool(hostile, "hostile", true);
	mach_msg_header_t *noise = calloc(1, sizeof(*noise));
	noise->msgh_size = sizeof(*noise);
	noise->msgh_local_port = bad_cl.local_name;
	noise->msgh_id = 999;
	bad_cl.local->refill_msg = noise;
	bad_cl.local->refill = 2000;
	inject(&bad_cl, hostile);
	xpc_release(hostile);
	xpc_object_t marked = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_bool(marked, "marked", true);
	host.late = client_message(marked, good.local_name, MACH_PORT_NULL, MACH_PORT_NULL);
	xpc_release(marked);
	host.watch = good.local;
	host.busy = bad_cl.local;
	run();
	CHECK(host.handled_marked == 1);
	CHECK(host.busy_at_watch > 0 && host.busy_at_watch <= 6);
	CHECK(bad_cl.local->received > 1000);
	host.watch = host.busy = NULL;
	CHECK(host.max_batch == 1);

	bad_cl.local->refill = 2000;
	struct kport *port = bad_cl.local;
	for (int i = 0; i < 3; i++) {
		xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
		inject(&bad_cl, m);
		xpc_release(m);
	}
	xpc_connection_cancel(bad_cl.peer);
	run();
	CHECK(!port->alive && port->refill > 1990);
	CHECK(entry_of(bad_cl.local_name) == NULL);
	finish(&good);
	CHECK(entry_of(bad_cl.remote_name) == NULL);
}

static int
live_once_rights(void)
{
	int n = 0;
	for (mach_port_t i = 1; i < next_index; i++) {
		if (names[i].port != NULL && names[i].once) {
			n++;
		}
	}
	return n;
}

static void
test_replies_and_ports(void)
{
	host.invalid = 0;
	host.replies = 0;
	host.reply_errors = 0;
	struct client cl = connect_client(2);
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);
	int once_before = live_once_rights();
	for (int i = 0; i < 20; i++) {
		xpc_object_t m = seq_message(i, i % 5 == 0 ? 30000 : 0);
		if (i % 3 == 0) {
			mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_SEND, 1);
			xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, carried);
			xpc_dictionary_set_value(m, "port", port);
			xpc_release(port);
		}
		if (i % 2 == 0) {
			xpc_connection_send_message_with_reply(cl.peer, m, NULL, ^(xpc_object_t r) {
				if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) {
					host.replies++;
				} else {
					host.reply_errors++;
				}
			});
		} else {
			xpc_connection_send_message(cl.peer, m);
		}
		xpc_release(m);
	}
	CHECK(queued(cl.peer) == 18);
	CHECK(NAME(carried).send == 1 + 6);
	int next = 0;
	bool ordered = true;
	for (int guard = 0; guard < 100 && next < 20; guard++) {
		while (cl.remote->count > 0) {
			int got = client_answer(&cl, next % 4 == 0);
			ordered = ordered && got == next;
			next++;
		}
		run();
	}
	CHECK(next == 20 && ordered);
	run();
	CHECK(host.replies == 5 && host.reply_errors == 5);
	CHECK(NAME(carried).send == 1);
	CHECK(live_once_rights() == once_before);
	CHECK(CONN(cl.peer)->pending == NULL);
	CHECK(host.vm_live == 0);
	finish(&cl);
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL);
}

static xpc_object_t
ports_message(int seq, mach_port_t port, unsigned n)
{
	xpc_object_t m = seq_message(seq, 0);
	mach_port_mod_refs(1, port, MACH_PORT_RIGHT_SEND, (mach_port_delta_t)n);
	for (unsigned i = 0; i < n; i++) {
		char key[16];
		snprintf(key, sizeof(key), "p%u", i);
		xpc_object_t o = _xpc_port_create(XPC_TYPE_MACH_SEND, port);
		xpc_dictionary_set_value(m, key, o);
		xpc_release(o);
	}
	return m;
}

static xpc_connection_t trap_peer;
static mach_port_t trap_port;
static int trap_kind;

static bool
child_traps(void (*fn)(void))
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
		fn();
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFSIGNALED(status) && WTERMSIG(status) == TRAP_SIGNAL;
}

static void
send_too_many(void)
{
	xpc_object_t m = ports_message(2, trap_port, 16384);
	if (trap_kind == 0) {
		xpc_connection_send_message(trap_peer, m);
	} else if (trap_kind == 1) {
		xpc_connection_send_message_with_reply(trap_peer, m, NULL, ^(xpc_object_t r) {
			(void)r;
		});
	} else {
		xpc_connection_send_message_with_reply_sync(trap_peer, m);
	}
}

static void
test_large_messages(void)
{
	host.invalid = 0;
	host.handled = 0;
	struct client cl = connect_client(4);
	struct conn *c = CONN(cl.peer);
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);

	const size_t big = (9u << 20) + 5;
	send_seq(&cl, 0, big);
	CHECK(cl.remote->count == 1);
	CHECK(cl.remote->head != NULL && ool_in(cl.remote->head->m) != NULL &&
	    ool_in(cl.remote->head->m)->size > big);
	CHECK(client_take(&cl) == 0);

	xpc_object_t m = ports_message(1, carried, 16383);
	xpc_connection_send_message(cl.peer, m);
	xpc_release(m);
	CHECK(cl.remote->count == 1 && NAME(carried).send == 1);
	if (cl.remote->head != NULL) {
		mach_msg_size_t n;
		ports_in(cl.remote->head->m, &n);
		CHECK(n == 16383 && cl.remote->head->m->msgh_size > 65536);
	}
	CHECK(client_take(&cl) == 1);

	trap_peer = cl.peer;
	trap_port = carried;
	for (int kind = 0; kind < 3; kind++) {
		trap_kind = kind;
		CHECK(child_traps(send_too_many));
	}
	CHECK(cl.remote->count == 0 && queued(cl.peer) == 0 && NAME(carried).send == 1);
	CHECK(!c->cancelled && host.invalid == 0);
	send_seq(&cl, 3, 0);
	CHECK(client_take(&cl) == 3);

	m = ports_message(4, carried, 6000);
	inject(&cl, m);
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_SEND, 6000);
	xpc_release(m);
	m = seq_message(5, big);
	inject(&cl, m);
	xpc_release(m);
	CHECK(cl.local->head != NULL && cl.local->head->m->msgh_size > 65536);
	int handled = host.handled;
	run();
	CHECK(host.handled == handled + 2 && cl.local->count == 0);
	CHECK(NAME(carried).send == 1);
	CHECK(host.vm_live == 0);
	finish(&cl);
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL);
}

static void
record(xpc_object_t e)
{
	char k = e == XPC_ERROR_CONNECTION_INVALID ? 'I' : e == XPC_ERROR_CONNECTION_INTERRUPTED ? 'R' :
	    xpc_get_type(e) == XPC_TYPE_CONNECTION ? 'C' : xpc_get_type(e) == XPC_TYPE_DICTIONARY ? 'M' :
	    '?';
	if (host.nevents < (int)sizeof(host.events) - 1) {
		host.events[host.nevents++] = k;
		host.events[host.nevents] = '\0';
	}
}

static void
events_reset(void)
{
	host.nevents = 0;
	host.events[0] = '\0';
}

static xpc_connection_t cancel_target;

static void
cancel_now(void)
{
	xpc_connection_cancel(cancel_target);
}

static void
test_cancel_races(void)
{
	struct client cl = connect_client(4);
	struct conn *c = CONN(cl.peer);
	xpc_connection_set_event_handler(cl.peer, ^(xpc_object_t e) {
		record(e);
	});
	events_reset();
	for (int i = 0; i < 3; i++) {
		xpc_object_t m = seq_message(i, 0);
		inject(&cl, m);
		xpc_release(m);
	}
	dispatch_suspend(c->q);
	run();
	CHECK(host.nevents == 0 && cl.local->count == 2);
	xpc_connection_cancel(cl.peer);
	xpc_object_t m = seq_message(9, 0);
	inject(&cl, m);
	xpc_release(m);
	dispatch_resume(c->q);
	run();
	CHECK(strcmp(host.events, "I") == 0);
	CHECK(entry_of(cl.local_name) == NULL && entry_of(cl.remote_name) == NULL);

	cl = connect_client(4);
	c = CONN(cl.peer);
	xpc_connection_set_event_handler(cl.peer, ^(xpc_object_t e) {
		record(e);
	});
	events_reset();
	m = seq_message(0, 0);
	inject(&cl, m);
	xpc_release(m);
	cancel_target = cl.peer;
	host.after_receive = cancel_now;
	run();
	CHECK(host.after_receive == NULL && strcmp(host.events, "I") == 0);
	CHECK(entry_of(cl.local_name) == NULL && entry_of(cl.remote_name) == NULL);

	mach_port_t saved = host.listener;
	for (int pass = 0; pass < 2; pass++) {
		events_reset();
		int peers = host.npeers;
		xpc_connection_t l = xpc_connection_create_mach_service("svc2", NULL,
		    XPC_CONNECTION_MACH_SERVICE_LISTENER);
		xpc_connection_set_event_handler(l, ^(xpc_object_t e) {
			record(e);
			if (xpc_get_type(e) == XPC_TYPE_CONNECTION) {
				xpc_connection_resume(e);
			}
		});
		xpc_connection_resume(l);
		run();
		struct client w = connect_begin(4);
		if (pass == 0) {
			cancel_target = l;
			host.after_receive = cancel_now;
			run();
			CHECK(host.after_receive == NULL);
		} else {
			dispatch_suspend(CONN(l)->q);
			run();
			CHECK(host.nevents == 0 && NAME(host.listener).port->count == 0);
			xpc_connection_cancel(l);
			dispatch_resume(CONN(l)->q);
			run();
		}
		CHECK(strcmp(host.events, "I") == 0 && host.npeers == peers);
		CHECK(entry_of(w.local_name) == NULL && entry_of(w.remote_name) == NULL);
		CHECK(entry_of(host.listener) == NULL);
		xpc_release(l);
	}
	host.listener = saved;

	cl = connect_client(1);
	send_seq(&cl, 0, 0);
	send_seq(&cl, 1, 0);
	CHECK(queued(cl.peer) == 1 && CONN(cl.peer)->sendq_dest == cl.remote_name);
	cancel_target = cl.peer;
	host.before_send = cancel_now;
	CHECK(client_take(&cl) == 0);
	run();
	CHECK(host.before_send == NULL && CONN(cl.peer)->cancelled);
	CHECK(client_take(&cl) == 1);
	CHECK(entry_of(cl.remote_name) == NULL && entry_of(cl.local_name) == NULL);
	CHECK(host.vm_live == 0);
}

static void
test_send_copy_fails(void)
{
	host.invalid = 0;
	host.logged = 0;
	host.malloc_failed = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	xpc_object_t m = seq_message(0, 2000);
	host.fail_malloc = 2000;
	xpc_connection_send_message(cl.peer, m);
	xpc_release(m);
	CHECK(host.malloc_failed == 1 && host.logged == 1 && cl.remote->count == 0);
	CHECK(queued(cl.peer) == 0 && NAME(cl.remote_name).send == 1 && !c->flushing);
	send_seq(&cl, 1, 0);
	send_seq(&cl, 2, 2000);
	send_seq(&cl, 3, 0);
	CHECK(queued(cl.peer) == 2);
	host.fail_malloc_size = c->sendq->msg.head->msgh_size + sizeof(mach_msg_max_trailer_t);
	CHECK(client_take(&cl) == 1);
	run();
	CHECK(host.malloc_failed == 2 && host.logged == 2 && queued(cl.peer) == 0);
	CHECK(client_take(&cl) == 3 && !c->cancelled && host.invalid == 0);
	CHECK(c->sendq_dest == MACH_PORT_NULL && NAME(cl.remote_name).send == 1);
	CHECK(host.vm_live == 0);
	finish(&cl);
}

static void
test_dead_port_in_queue(void)
{
	struct client cl = connect_client(1);
	send_seq(&cl, 0, 0);
	struct kport *gone = port_new(false, 4);
	mach_port_t name = name_new(gone);
	NAME(name).send = 1;
	xpc_object_t m = seq_message(1, 0);
	xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, name);
	xpc_dictionary_set_value(m, "port", port);
	xpc_release(port);
	port_destroy(gone);
	CHECK(NAME(name).dead);
	xpc_connection_send_message(cl.peer, m);
	CHECK(queued(cl.peer) == 1);
	xpc_release(m);
	CHECK(entry_of(name) == NULL);
	struct kport *other = port_new(false, 4);
	mach_port_t reused = name_wrap(name, other);
	NAME(reused).send = 1;
	CHECK(client_take(&cl) == 0);
	run();
	CHECK(cl.remote->count == 1);
	if (cl.remote->head != NULL) {
		mach_msg_size_t n;
		mach_msg_port_descriptor_t *pd = ports_in(cl.remote->head->m, &n);
		CHECK(n == 1 && pd[0].name == MACH_PORT_NULL);
	}
	CHECK(NAME(reused).send == 1);
	CHECK(client_take(&cl) == -1);
	finish(&cl);
	mach_port_deallocate(1, reused);
}

static void
test_client_dies(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	for (int i = 0; i < 3; i++) {
		send_seq(&cl, i, 0);
	}
	CHECK(queued(cl.peer) == 2 && cl.remote->sp_armed);
	client_die(&cl);
	run();
	CHECK(CONN(cl.peer)->cancelled && host.invalid == 1);
	CHECK(entry_of(cl.remote_name) == NULL);
	CHECK(entry_of(cl.local_name) == NULL);

	host.hold = true;
	host.invalid = 0;
	host.handled = 0;
	struct client held = connect_client(1);
	for (int i = 0; i < 20; i++) {
		xpc_object_t m = seq_message(i, 0);
		inject(&held, m);
		xpc_release(m);
	}
	run();
	CHECK(held.local->count == 20);
	client_die(&held);
	run();
	CHECK(!CONN(held.peer)->cancelled && host.handled == 0);
	host.hold = false;
	xpc_connection_resume(held.peer);
	run();
	CHECK(host.handled == 21);
	CHECK(CONN(held.peer)->cancelled && host.invalid == 1);
	CHECK(entry_of(held.remote_name) == NULL);
	CHECK(entry_of(held.local_name) == NULL);
	CHECK(host.vm_live == 0);
}

static void
test_rejected_handshakes(void)
{
	host.reject = true;
	rejected = 0;
	int before = host.npeers;
	struct client cl[6];
	for (int i = 0; i < 6; i++) {
		host.reject_suspended = i >= 3;
		cl[i] = connect_data(1, (8u << 20) - 4096);
		for (int j = 0; j < 3; j++) {
			xpc_object_t m = seq_message(j, 100000);
			inject(&cl[i], m);
			xpc_release(m);
		}
		run();
	}
	host.reject_suspended = false;
	CHECK(host.npeers == before);
	CHECK(rejected == 6);
	for (int i = 0; i < 6; i++) {
		CHECK(entry_of(cl[i].remote_name) == NULL);
		CHECK(entry_of(cl[i].local_name) == NULL);
	}
	host.reject = false;
	struct client ok = connect_client(1);
	CHECK(host.npeers == before + 1);
	finish(&ok);
	CHECK(host.vm_live == 0);
}

static xpc_connection_t
client_new(void)
{
	xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		if (e == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.interrupted++;
		} else if (e == XPC_ERROR_CONNECTION_INVALID) {
			host.client_invalid++;
		}
	});
	xpc_connection_resume(x);
	return x;
}

static void
client_send(xpc_connection_t x, int seq, size_t data)
{
	xpc_object_t m = seq_message(seq, data);
	xpc_connection_send_message(x, m);
	xpc_release(m);
}

static struct kport *
client_remote(xpc_connection_t x)
{
	mach_port_t r = CONN(x)->remote;
	return entry_of(r) != NULL ? NAME(r).port : NULL;
}

static int
port_take(struct kport *p, bool answer)
{
	struct client s = { .remote = p };
	return client_answer(&s, answer);
}

static void
service_fill(void)
{
	while (host.service->count < host.service->qlimit) {
		xpc_object_t junk = seq_message(-5, 0);
		kq_put(host.service, client_message(junk, MACH_PORT_NULL, MACH_PORT_NULL, MACH_PORT_NULL),
		    1234, NULL, NULL);
		xpc_release(junk);
	}
}

static void
test_client_queue(void)
{
	host.service = port_new(false, 1);
	host.interrupted = 0;
	host.client_invalid = 0;
	service_fill();
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	const int total = 20000;
	client_send(x, 0, 0);
	CHECK(queued(x) == 1 && host.service->sp_armed);
	mach_port_t rname = c->remote, lname = c->local;
	struct kport *rp = client_remote(x);
	rp->qlimit = 1;
	size_t big = 0;
	for (int i = 1; i < total; i++) {
		size_t data = i == 3 ? (8u << 20) - 4096 : i % 250 == 1 ? (1u << 20) : 0;
		big += data;
		client_send(x, i, data);
	}
	CHECK(big > (64u << 20));
	CHECK(queued(x) == (size_t)total);
	CHECK(NAME(rname).send == 2 && NAME(rname).recv);
	CHECK(port_take(host.service, false) == -5);
	run();
	CHECK(host.service->count == 1 && port_take(host.service, false) == 0);
	CHECK(!rp->ours && !NAME(rname).recv);
	int next = 1;
	bool ordered = true, urefs = true;
	for (int guard = 0; guard < 2 * total && next < total; guard++) {
		while (rp->count > 0) {
			int got = port_take(rp, false);
			ordered = ordered && got == next;
			next++;
		}
		run();
		urefs = urefs && NAME(rname).send <= 2;
	}
	CHECK(next == total && ordered && urefs);
	CHECK(c->sendq == NULL && c->sendq_dest == MACH_PORT_NULL);
	CHECK(NAME(rname).send == 1);
	CHECK(watching_death(rp) && !c->cancelled && host.interrupted == 0);
	CHECK(host.vm_live == 0);
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && host.interrupted == 0);
	CHECK(entry_of(rname) == NULL && entry_of(lname) == NULL);
	CHECK(entry_of(host.service_name) == NULL);
	CHECK(*note_find(rname) == NULL && *note_find(host.service_name) == NULL);
	xpc_release(x);
	port_destroy(host.service);
}

static void
sync_step(void)
{
	struct kport *rp = host.sync_remote;
	if (rp->count > 0) {
		int got = port_take(rp, true);
		host.sync_ordered = host.sync_ordered && got == host.sync_next;
		host.sync_next++;
	}
	run();
}

static void
test_client_sync(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	CHECK(queued(x) == 0 && port_take(host.service, false) == 0);
	mach_port_t rname = c->remote;
	struct kport *rp = client_remote(x);
	rp->qlimit = 1;
	for (int i = 1; i < 6; i++) {
		client_send(x, i, i == 2 ? 100000 : 0);
	}
	CHECK(rp->count == 1 && queued(x) == 4);
	host.sync_remote = rp;
	host.sync_next = 1;
	host.sync_ordered = true;
	host.blocked = sync_step;
	xpc_object_t req = seq_message(6, 0);
	xpc_object_t reply = xpc_connection_send_message_with_reply_sync(x, req);
	CHECK(xpc_get_type(reply) == XPC_TYPE_DICTIONARY);
	CHECK(xpc_dictionary_get_int64(reply, "seq") == 6);
	CHECK(host.sync_next == 7 && host.sync_ordered);
	CHECK(queued(x) == 0 && rp->count == 0);
	xpc_release(reply);
	host.sync_next = 7;
	reply = xpc_connection_send_message_with_reply_sync(x, req);
	CHECK(xpc_get_type(reply) == XPC_TYPE_DICTIONARY && host.sync_next == 8);
	xpc_release(reply);
	host.sync_ordered = true;
	client_send(x, 8, 0);
	CHECK(rp->count == 1 && queued(x) == 0);
	xpc_object_t full = seq_message(9, 0);
	reply = xpc_connection_send_message_with_reply_sync(x, full);
	xpc_release(full);
	CHECK(xpc_get_type(reply) == XPC_TYPE_DICTIONARY && xpc_dictionary_get_int64(reply, "seq") == 9);
	CHECK(host.sync_next == 10 && host.sync_ordered && host.interrupted == 0);
	xpc_release(reply);
	host.blocked = NULL;
	xpc_release(req);
	CHECK(entry_of(mig_port) != NULL && NAME(mig_port).port->count == 0);
	CHECK(host.interrupted == 0 && NAME(rname).send == 1);
	CHECK(host.vm_live == 0);
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL);
	xpc_release(x);
	port_destroy(host.service);
}

static void
test_client_server_dies(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	host.replies = 0;
	host.reply_errors = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	mach_port_t rname = c->remote, lname = c->local;
	struct kport *rp = client_remote(x);
	rp->qlimit = 1;
	for (int i = 1; i < 5; i++) {
		client_send(x, i, i == 3 ? 100000 : 0);
	}
	xpc_object_t m = seq_message(5, 0);
	xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
		if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) {
			host.replies++;
		} else if (r == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.reply_errors++;
		}
	});
	xpc_release(m);
	CHECK(queued(x) == 4 && rp->sp_armed);
	CHECK(NAME(rname).send == 2);
	port_destroy(rp);
	CHECK(NAME(rname).send == 3);
	run();
	CHECK(host.interrupted == 1 && !c->cancelled);
	CHECK(host.replies == 0 && host.reply_errors == 1);
	CHECK(c->sendq == NULL && c->pending == NULL && !MACH_PORT_VALID(c->remote));
	CHECK(entry_of(rname) == NULL && *note_find(rname) == NULL);
	CHECK(host.vm_live == 0);

	client_send(x, 10, 0);
	CHECK(port_take(host.service, false) == 10);
	rname = c->remote;
	rp = client_remote(x);
	rp->qlimit = 1;
	client_send(x, 11, 0);
	client_send(x, 12, 0);
	CHECK(queued(x) == 1 && rp->sp_armed);
	CHECK(port_take(rp, false) == 11);
	run();
	CHECK(port_take(rp, false) == 12);
	run();
	CHECK(queued(x) == 0 && watching_death(rp));
	port_destroy(rp);
	run();
	CHECK(host.interrupted == 2 && !c->cancelled);
	CHECK(entry_of(rname) == NULL && *note_find(rname) == NULL);

	service_fill();
	client_send(x, 20, 0);
	client_send(x, 21, 0);
	CHECK(queued(x) == 2 && host.service->sp_armed);
	rname = c->remote;
	mach_port_t sname = host.service_name;
	CHECK(NAME(rname).recv && NAME(sname).send == 1);
	port_destroy(host.service);
	run();
	CHECK(host.interrupted == 3 && !c->cancelled && queued(x) == 0);
	CHECK(entry_of(rname) == NULL && entry_of(sname) == NULL);
	CHECK(*note_find(sname) == NULL);
	client_send(x, 30, 0);
	CHECK(c->cancelled);
	run();
	CHECK(host.client_invalid == 1 && entry_of(lname) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
}

static xpc_connection_t stale_client;
static mach_port_t stale_remote;

static void
stale_reconnect(void)
{
	port_destroy(NAME(stale_remote).port);
	notify_receive();
	CHECK(!MACH_PORT_VALID(CONN(stale_client)->remote));
	run();
	client_send(stale_client, 99, 0);
}

static void
stale_reconnect_reply(void)
{
	port_destroy(NAME(stale_remote).port);
	notify_receive();
	run();
	xpc_object_t m = seq_message(98, 0);
	xpc_dictionary_set_value(m, "connection", stale_client);
	xpc_connection_send_message_with_reply(stale_client, m, NULL, ^(xpc_object_t r) {
		if (r == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.reply_errors++;
		}
	});
	xpc_release(m);
	CHECK(!CONN(stale_client)->draining && MACH_PORT_VALID(CONN(stale_client)->service));
	client_send(stale_client, 99, 0);
}

static void
test_server_dies_with_messages(void)
{
	host.service = port_new(false, 4);
	host.replies = 0;
	host.reply_errors = 0;
	events_reset();
	xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		record(e);
	});
	xpc_connection_resume(x);
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	struct kport *rp = client_remote(x);
	xpc_object_t m = seq_message(1, 0);
	xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
		if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) {
			host.replies++;
		} else {
			host.reply_errors++;
		}
	});
	xpc_release(m);
	run();
	CHECK(port_take(rp, true) == 1);
	struct kport *local = NAME(c->local).port;
	for (int i = 0; i < 3; i++) {
		m = seq_message(10 + i, i == 1 ? 100000 : 0);
		kq_put(local, client_message(m, c->local, MACH_PORT_NULL, MACH_PORT_NULL), 1234, NULL,
		    NULL);
		xpc_release(m);
	}
	dispatch_suspend(c->q);
	run();
	CHECK(local->count == 2 && host.nevents == 0);
	port_destroy(rp);
	run();
	CHECK(local->count == 0 && host.nevents == 0);
	dispatch_resume(c->q);
	run();
	CHECK(strcmp(host.events, "MMMR") == 0);
	CHECK(host.replies == 1 && host.reply_errors == 0 && !c->cancelled);
	xpc_connection_cancel(x);
	run();
	CHECK(strcmp(host.events, "MMMRI") == 0);
	xpc_release(x);

	events_reset();
	x = xpc_connection_create_mach_service("svc", NULL, 0);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		record(e);
	});
	xpc_connection_resume(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	run();
	port_destroy(client_remote(x));
	notify_receive();
	xpc_connection_cancel(x);
	run();
	CHECK(strcmp(host.events, "I") == 0);
	xpc_release(x);

	events_reset();
	x = xpc_connection_create_mach_service("svc", NULL, 0);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		record(e);
	});
	xpc_connection_resume(x);
	c = CONN(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	run();
	local = NAME(c->local).port;
	for (int i = 0; i < 2; i++) {
		m = seq_message(20 + i, 0);
		kq_put(local, client_message(m, c->local, MACH_PORT_NULL, MACH_PORT_NULL), 1234, NULL,
		    NULL);
		xpc_release(m);
	}
	port_destroy(client_remote(x));
	notify_receive();
	int lookups = host.lookups;
	client_send(x, 1, 0);
	CHECK(c->draining && queued(x) == 1 && host.lookups == lookups && host.service->count == 0);
	run();
	CHECK(strcmp(host.events, "MMR") == 0 && !c->draining);
	CHECK(host.lookups == lookups + 1 && port_take(host.service, false) == 1);
	m = seq_message(30, 0);
	kq_put(local, client_message(m, c->local, MACH_PORT_NULL, MACH_PORT_NULL), 1234, NULL, NULL);
	xpc_release(m);
	run();
	CHECK(strcmp(host.events, "MMRM") == 0);
	xpc_connection_cancel(x);
	run();
	CHECK(strcmp(host.events, "MMRMI") == 0);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static void
test_peer_remote_dies(void)
{
	host.invalid = 0;
	host.peer_interrupted = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	send_seq(&cl, 0, 0);
	send_seq(&cl, 1, 0);
	CHECK(queued(cl.peer) == 1 && cl.remote->sp_armed);
	port_destroy(cl.remote);
	run();
	CHECK(c->cancelled && host.invalid == 1 && host.peer_interrupted == 0);
	CHECK(entry_of(cl.remote_name) == NULL && entry_of(cl.local_name) == NULL);
	CHECK(host.vm_live == 0);
}

static void
test_privileged_client(void)
{
	host.service = port_new(false, 4);
	host.replies = 0;
	host.reply_errors = 0;
	host.sender_euid = 501;
	events_reset();
	xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL,
	    XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		record(e);
	});
	xpc_connection_resume(x);
	struct conn *c = CONN(x);
	xpc_object_t m = seq_message(0, 0);
	xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
		if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) {
			host.replies++;
		} else {
			host.reply_errors++;
		}
	});
	xpc_release(m);
	CHECK(host.lookup_flags == BOOTSTRAP_PRIVILEGED_SERVER);
	CHECK(port_take(host.service, true) == 0);
	m = seq_message(1, 0);
	kq_put(NAME(c->local).port, client_message(m, c->local, MACH_PORT_NULL, MACH_PORT_NULL),
	    1234, NULL, NULL);
	xpc_release(m);
	run();
	CHECK(host.replies == 1 && host.reply_errors == 0 && strcmp(host.events, "M") == 0);
	CHECK(xpc_connection_get_euid(x) == 501);
	struct kport *rp = client_remote(x);
	host.sync_remote = rp;
	host.sync_next = 2;
	host.sync_ordered = true;
	host.blocked = sync_step;
	m = seq_message(2, 0);
	xpc_object_t reply = xpc_connection_send_message_with_reply_sync(x, m);
	host.blocked = NULL;
	CHECK(xpc_get_type(reply) == XPC_TYPE_DICTIONARY && xpc_dictionary_get_int64(reply, "seq") == 2);
	xpc_release(reply);
	xpc_release(m);
	host.sender_euid = 0;
	xpc_connection_cancel(x);
	run();
	CHECK(strcmp(host.events, "MI") == 0);
	xpc_release(x);
	x = client_new();
	client_send(x, 3, 0);
	CHECK(host.lookup_flags == 0 && port_take(host.service, false) == 3);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(host.vm_live == 0);
	port_destroy(host.service);
}

static mach_port_t
request_from(struct client *cl, xpc_object_t dict, struct kport *answer)
{
	mach_msg_header_t *m = client_message(dict, cl->local_name, MACH_PORT_NULL, MACH_PORT_NULL);
	mach_port_t once = once_new(answer);
	m->msgh_id = XPC_WIRE_REQUEST;
	m->msgh_remote_port = once;
	m->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_PORT_SEND_ONCE, MACH_MSG_TYPE_PORT_SEND, 0,
	    MACH_MSGH_BITS_COMPLEX);
	kq_put(cl->local, m, 1234, NULL, NULL);
	return once;
}

static xpc_object_t sync_after_cancel;
static int asked;

static void
reply_again(xpc_connection_t peer, xpc_object_t r)
{
	xpc_dictionary_set_int64(r, "seq", 100);
	xpc_connection_send_message_with_reply(peer, r, NULL, ^(xpc_object_t again) {
		if (xpc_get_type(again) == XPC_TYPE_DICTIONARY &&
		    xpc_dictionary_get_int64(again, "seq") == 100) {
			host.replies++;
		} else {
			host.reply_errors++;
		}
	});
}

static void
set_reply_handler(xpc_connection_t peer)
{
	xpc_connection_set_event_handler(peer, ^(xpc_object_t e) {
		if (xpc_get_type(e) != XPC_TYPE_DICTIONARY) {
			return;
		}
		asked++;
		xpc_object_t r = xpc_dictionary_create_reply(e);
		if (xpc_dictionary_get_bool(e, "suspend")) {
			xpc_connection_suspend(peer);
			if (xpc_dictionary_get_bool(e, "plain")) {
				xpc_connection_send_message(peer, r);
			} else {
				reply_again(peer, r);
			}
			xpc_release(r);
			return;
		}
		if (xpc_dictionary_get_bool(e, "badqueued")) {
			xpc_dictionary_set_value(r, "connection", peer);
			reply_again(peer, r);
			xpc_release(r);
			return;
		}
		if (xpc_dictionary_get_bool(e, "cancelplain")) {
			xpc_connection_cancel(peer);
			xpc_connection_send_message(peer, r);
			xpc_release(r);
			return;
		}
		if (xpc_dictionary_get_bool(e, "cancel")) {
			xpc_connection_cancel(peer);
			sync_after_cancel = xpc_connection_send_message_with_reply_sync(peer, r);
			xpc_release(r);
			return;
		}
		if (xpc_dictionary_get_bool(e, "bad")) {
			xpc_dictionary_set_value(r, "connection", peer);
			xpc_connection_send_message(peer, r);
			xpc_release(r);
			return;
		}
		reply_again(peer, r);
		xpc_release(r);
	});
}

static struct kmsg *
answer_head(struct kport *answer, mach_msg_id_t id)
{
	struct kmsg *k = answer->count == 1 ? answer->head : NULL;
	return k != NULL && k->m->msgh_id == id ? k : NULL;
}

static void
test_reply_with_reply(void)
{
	host.replies = 0;
	host.reply_errors = 0;
	struct client cl = connect_client(4);
	xpc_connection_t peer = cl.peer;
	asked = 0;
	set_reply_handler(peer);
	struct kport *answer = port_new(false, 4);
	xpc_object_t m = seq_message(1, 0);
	request_from(&cl, m, answer);
	xpc_release(m);
	run();
	CHECK(asked == 1 && answer->count == 1);
	if (answer->head != NULL) {
		CHECK(answer->head->m->msgh_id == XPC_WIRE_REPLY && answer->head->reply != NULL);
	}
	CHECK(port_take(answer, true) == 100);
	run();
	CHECK(host.replies == 1 && host.reply_errors == 0 && CONN(peer)->pending == NULL);
	CHECK(live_once_rights() == 0);
	host.logged = 0;
	m = seq_message(2, 0);
	xpc_dictionary_set_bool(m, "bad", true);
	request_from(&cl, m, answer);
	xpc_release(m);
	run();
	CHECK(asked == 2 && answer->count == 1 && host.logged == 1);
	if (answer->head != NULL) {
		CHECK(answer->head->m->msgh_id == MACH_NOTIFY_SEND_ONCE);
		kmsg_free(kq_take(answer));
	}
	run();
	CHECK(live_once_rights() == 0);

	m = seq_message(3, 0);
	xpc_dictionary_set_bool(m, "suspend", true);
	xpc_dictionary_set_bool(m, "plain", true);
	request_from(&cl, m, answer);
	xpc_release(m);
	run();
	CHECK(asked == 3 && answer_head(answer, XPC_WIRE_REPLY) != NULL);
	if (answer->head != NULL) {
		kmsg_free(kq_take(answer));
	}
	xpc_connection_resume(peer);
	run();
	m = seq_message(4, 0);
	xpc_dictionary_set_bool(m, "suspend", true);
	request_from(&cl, m, answer);
	xpc_release(m);
	run();
	CHECK(asked == 4 && answer->count == 0 && queued(peer) == 1);
	xpc_connection_resume(peer);
	CHECK(answer_head(answer, XPC_WIRE_REPLY) != NULL && queued(peer) == 0);
	CHECK(port_take(answer, true) == 100);
	run();
	CHECK(host.replies == 2 && host.reply_errors == 0 && CONN(peer)->pending == NULL);

	m = seq_message(5, 0);
	xpc_dictionary_set_bool(m, "cancel", true);
	request_from(&cl, m, answer);
	xpc_release(m);
	run();
	CHECK(asked == 5 && sync_after_cancel == XPC_ERROR_CONNECTION_INVALID);
	CHECK(answer->count == 1 && answer->head != NULL &&
	    answer->head->m->msgh_id == MACH_NOTIFY_SEND_ONCE);
	if (answer->head != NULL) {
		kmsg_free(kq_take(answer));
	}
	CHECK(live_once_rights() == 0);
	finish(&cl);

	for (int pass = 0; pass < 5; pass++) {
		struct client c2 = connect_client(4);
		set_reply_handler(c2.peer);
		int errors = host.reply_errors;
		host.logged = 0;
		m = seq_message(6, 0);
		xpc_dictionary_set_bool(m, pass == 0 ? "cancelplain" : pass == 4 ? "badqueued" :
		    "suspend", true);
		struct kport *to = pass == 2 ? port_new(false, 4) : answer;
		request_from(&c2, m, to);
		xpc_release(m);
		run();
		if (pass == 4) {
			CHECK(answer_head(answer, MACH_NOTIFY_SEND_ONCE) != NULL && host.logged == 1);
			if (answer->head != NULL) {
				kmsg_free(kq_take(answer));
			}
			CHECK(host.reply_errors == errors + 1 && CONN(c2.peer)->pending == NULL);
			CHECK(!CONN(c2.peer)->cancelled && queued(c2.peer) == 0 && live_once_rights() == 0);
			finish(&c2);
			continue;
		}
		if (pass == 3) {
			CHECK(answer->count == 0 && queued(c2.peer) == 1);
			host.logged = 0;
			host.malloc_failed = 0;
			host.fail_malloc = 100;
			xpc_connection_resume(c2.peer);
			run();
			CHECK(host.malloc_failed == 1 && host.logged == 1 && queued(c2.peer) == 0);
			CHECK(answer_head(answer, MACH_NOTIFY_SEND_ONCE) != NULL);
			if (answer->head != NULL) {
				kmsg_free(kq_take(answer));
			}
			CHECK(host.reply_errors == errors + 1 && CONN(c2.peer)->pending == NULL);
			CHECK(!CONN(c2.peer)->cancelled && live_once_rights() == 0);
			finish(&c2);
			continue;
		}
		if (pass == 2) {
			CHECK(to->count == 0 && queued(c2.peer) == 1);
			port_destroy(to);
			free(to);
			xpc_connection_resume(c2.peer);
			run();
			CHECK(queued(c2.peer) == 0 && !CONN(c2.peer)->cancelled);
			CHECK(host.reply_errors == errors + 1 && CONN(c2.peer)->pending == NULL);
			send_seq(&c2, 7, 0);
			CHECK(client_take(&c2) == 7);
			finish(&c2);
			continue;
		}
		if (pass == 1) {
			CHECK(answer->count == 0 && queued(c2.peer) == 1);
			xpc_connection_cancel(c2.peer);
			run();
			xpc_connection_resume(c2.peer);
			run();
			CHECK(host.reply_errors == errors + 1 && CONN(c2.peer)->pending == NULL);
		}
		CHECK(answer_head(answer, MACH_NOTIFY_SEND_ONCE) != NULL);
		if (answer->head != NULL) {
			kmsg_free(kq_take(answer));
		}
		CHECK(live_once_rights() == 0);
		finish(&c2);
	}
	free(answer);

	host.service = port_new(false, 4);
	host.replies = 0;
	host.reply_errors = 0;
	xpc_connection_t x = client_new();
	xpc_object_t first = seq_message(5, 0);
	static xpc_object_t got;
	got = NULL;
	xpc_connection_send_message_with_reply(x, first, NULL, ^(xpc_object_t r) {
		got = xpc_retain(r);
	});
	xpc_release(first);
	struct kmsg *k = kq_take(host.service);
	CHECK(k != NULL && k->reply != NULL);
	if (k != NULL && k->reply != NULL) {
		xpc_object_t r = seq_message(6, 0);
		mach_msg_header_t *rm = client_message(r, k->reply->recv_name, MACH_PORT_NULL,
		    MACH_PORT_NULL);
		xpc_release(r);
		rm->msgh_id = XPC_WIRE_REPLY;
		rm->msgh_remote_port = once_new(answer = port_new(false, 4));
		rm->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_PORT_SEND_ONCE, 0, 0,
		    MACH_MSGH_BITS_COMPLEX);
		kq_put(k->reply, rm, 1234, NULL, NULL);
		k->reply = NULL;
		kmsg_free(k);
		run();
		CHECK(got != NULL && xpc_get_type(got) == XPC_TYPE_DICTIONARY &&
		    xpc_dictionary_get_int64(got, "seq") == 6);
		xpc_object_t back = xpc_dictionary_create_reply(got);
		CHECK(back != NULL);
		xpc_dictionary_set_int64(back, "seq", 7);
		host.sync_remote = answer;
		host.sync_next = 7;
		host.sync_ordered = true;
		host.blocked = sync_step;
		xpc_object_t last = xpc_connection_send_message_with_reply_sync(x, back);
		host.blocked = NULL;
		CHECK(xpc_get_type(last) == XPC_TYPE_DICTIONARY &&
		    xpc_dictionary_get_int64(last, "seq") == 7 && host.sync_next == 8);
		xpc_release(last);
		xpc_release(back);
		xpc_release(got);
		free(answer);

		got = NULL;
		first = seq_message(8, 0);
		xpc_connection_send_message_with_reply(x, first, NULL, ^(xpc_object_t r) {
			got = xpc_retain(r);
		});
		xpc_release(first);
		k = kq_take(client_remote(x));
		CHECK(k != NULL && k->reply != NULL);
		r = seq_message(9, 0);
		rm = client_message(r, k->reply->recv_name, MACH_PORT_NULL, MACH_PORT_NULL);
		xpc_release(r);
		rm->msgh_id = XPC_WIRE_REPLY;
		rm->msgh_remote_port = once_new(answer = port_new(false, 4));
		rm->msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_PORT_SEND_ONCE, 0, 0,
		    MACH_MSGH_BITS_COMPLEX);
		kq_put(k->reply, rm, 1234, NULL, NULL);
		k->reply = NULL;
		kmsg_free(k);
		run();
		CHECK(got != NULL && xpc_dictionary_get_int64(got, "seq") == 9);
		back = xpc_dictionary_create_reply(got);
		mach_port_t mig = mig_get_reply_port();
		host.fail_send = MACH_SEND_INVALID_RIGHT;
		last = xpc_connection_send_message_with_reply_sync(x, back);
		CHECK(last == XPC_ERROR_CONNECTION_INTERRUPTED && host.fail_send == KERN_SUCCESS);
		CHECK(mig_port == mig && NAME(mig).port->count == 0);
		xpc_release(back);
		xpc_release(got);
		free(answer);
	}
	CHECK(live_once_rights() == 0);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(host.vm_live == 0);
	port_destroy(host.service);
}

static xpc_connection_t sync_cancel_target;
static int sync_cancel_calls;

static void
sync_cancel(void)
{
	if (sync_cancel_calls++ == 0) {
		xpc_connection_cancel(sync_cancel_target);
		run();
	}
}

static void
test_cancel_wakes_sync(void)
{
	for (int pass = 0; pass < 3; pass++) {
		host.service = port_new(false, pass == 1 ? 1 : 4);
		host.client_invalid = 0;
		host.interrupted = 0;
		int once = live_once_rights();
		xpc_connection_t x = client_new();
		if (pass == 1) {
			service_fill();
		}
		client_send(x, 0, 0);
		mach_port_t reply_port = mig_get_reply_port();
		sync_cancel_target = x;
		sync_cancel_calls = 0;
		host.blocked = sync_cancel;
		if (pass == 2) {
			xpc_connection_cancel(x);
			run();
		}
		xpc_object_t m = seq_message(1, 0);
		xpc_object_t r = xpc_connection_send_message_with_reply_sync(x, m);
		xpc_release(m);
		host.blocked = NULL;
		CHECK(r == XPC_ERROR_CONNECTION_INVALID);
		CHECK(sync_cancel_calls == (pass == 2 ? 0 : 1));
		CHECK(CONN(x)->pending == NULL);
		if (pass != 2) {
			CHECK(entry_of(reply_port) == NULL && !MACH_PORT_VALID(mig_port));
		} else {
			CHECK(entry_of(reply_port) != NULL && NAME(reply_port).port->count == 0);
		}
		run();
		CHECK(host.client_invalid == 1 && host.interrupted == 0);
		CHECK(live_once_rights() == once);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static void
refuse_now(void)
{
	host.fail_send = MACH_SEND_TOO_LARGE;
}

static void
refuse_next(void)
{
	host.before_send = refuse_now;
}

static void
test_bad_messages(void)
{
	host.service = port_new(false, 4);
	host.client_invalid = 0;
	host.interrupted = 0;
	host.replies = 0;
	host.reply_errors = 0;
	host.logged = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	struct kport *rp = client_remote(x);
	xpc_object_t bad = seq_message(1, 0);
	xpc_dictionary_set_value(bad, "connection", x);
	xpc_connection_send_message(x, bad);
	CHECK(host.logged == 1 && rp->count == 0);
	xpc_connection_send_message_with_reply(x, bad, NULL, ^(xpc_object_t r) {
		if (r == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.reply_errors++;
		} else {
			host.replies++;
		}
	});
	CHECK(host.logged == 2);
	mach_port_t mig = mig_get_reply_port();
	xpc_object_t r = xpc_connection_send_message_with_reply_sync(x, bad);
	CHECK(r == XPC_ERROR_CONNECTION_INTERRUPTED && host.logged == 3);
	CHECK(mig_port == mig && NAME(mig).port->count == 0);
	xpc_release(bad);
	run();
	CHECK(host.reply_errors == 1 && host.replies == 0 && c->pending == NULL);
	CHECK(rp->count == 0 && !c->cancelled && host.interrupted == 0);

	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);
	rp->qlimit = 1;
	client_send(x, 2, 0);
	client_send(x, 3, 0);
	xpc_object_t many = ports_message(4, carried, 1000);
	xpc_connection_send_message_with_reply(x, many, NULL, ^(xpc_object_t e) {
		if (e == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.reply_errors++;
		} else {
			host.replies++;
		}
	});
	xpc_release(many);
	client_send(x, 5, 0);
	CHECK(queued(x) == 3 && rp->count == 1 && host.logged == 3);
	CHECK(port_take(rp, false) == 2);
	host.before_send = refuse_next;
	run();
	CHECK(host.logged == 4 && queued(x) == 1 && rp->count == 1);
	CHECK(port_take(rp, false) == 3);
	run();
	CHECK(queued(x) == 0 && port_take(rp, false) == 5);
	run();
	CHECK(host.reply_errors == 2 && host.replies == 0 && c->pending == NULL);
	CHECK(!c->cancelled && host.interrupted == 0 && host.client_invalid == 0);
	CHECK(NAME(carried).send == 1);
	client_send(x, 6, 0);
	CHECK(port_take(rp, false) == 6);
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && host.interrupted == 0);
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static struct client *barrier_client;
static int barrier_runs;
static unsigned barrier_seen_count;
static size_t barrier_seen_queued;

static void
test_send_barrier(void)
{
	host.invalid = 0;
	struct client cl = connect_client(1);
	struct conn *c = CONN(cl.peer);
	barrier_client = &cl;
	barrier_runs = 0;
	send_seq(&cl, 0, 0);
	send_seq(&cl, 1, 0);
	send_seq(&cl, 2, 0);
	CHECK(cl.remote->count == 1 && queued(cl.peer) == 2);
	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
		barrier_seen_count = barrier_client->remote->count;
		barrier_seen_queued = queued(barrier_client->peer);
		send_seq(barrier_client, 4, 0);
		barrier_seen_queued += queued(barrier_client->peer) * 10;
	});
	send_seq(&cl, 3, 0);
	CHECK(queued(cl.peer) == 4);
	run();
	CHECK(barrier_runs == 0);
	CHECK(client_take(&cl) == 0);
	run();
	CHECK(barrier_runs == 0 && cl.remote->count == 1);
	CHECK(client_take(&cl) == 1);
	run();
	CHECK(barrier_runs == 1 && barrier_seen_count == 1 && barrier_seen_queued == 1 + 20);
	CHECK(cl.remote->count == 1 && queued(cl.peer) == 2);
	CHECK(client_take(&cl) == 2);
	run();
	CHECK(client_take(&cl) == 3);
	run();
	CHECK(client_take(&cl) == 4);
	run();
	CHECK(queued(cl.peer) == 0 && c->sendq_dest == MACH_PORT_NULL && !c->flushing);

	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
	});
	CHECK(queued(cl.peer) == 0 && barrier_runs == 1);
	run();
	CHECK(barrier_runs == 2);

	send_seq(&cl, 7, 0);
	send_seq(&cl, 8, 0);
	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
	});
	CHECK(queued(cl.peer) == 2 && c->sendq_dest == cl.remote_name);
	CHECK(client_take(&cl) == 7);
	run();
	CHECK(barrier_runs == 3 && c->sendq_dest == MACH_PORT_NULL && NAME(cl.remote_name).send == 1);
	CHECK(client_take(&cl) == 8);

	cl.remote->qlimit = 4;
	dispatch_suspend(c->q);
	send_seq(&cl, 9, 0);
	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
	});
	send_seq(&cl, 10, 0);
	run();
	CHECK(barrier_runs == 3 && cl.remote->count == 1 && queued(cl.peer) == 1);
	dispatch_resume(c->q);
	run();
	CHECK(barrier_runs == 4 && cl.remote->count == 2 && queued(cl.peer) == 0);
	CHECK(client_take(&cl) == 9 && client_take(&cl) == 10);
	cl.remote->qlimit = 1;

	send_seq(&cl, 5, 0);
	send_seq(&cl, 6, 0);
	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
	});
	CHECK(queued(cl.peer) == 2);
	xpc_connection_cancel(cl.peer);
	run();
	CHECK(barrier_runs == 5 && host.invalid == 1);
	xpc_connection_send_barrier(cl.peer, ^{
		barrier_runs++;
	});
	run();
	CHECK(barrier_runs == 6);
	CHECK(entry_of(cl.remote_name) == NULL && entry_of(cl.local_name) == NULL);

	struct client cl2 = connect_client(1);
	barrier_client = &cl2;
	send_seq(&cl2, 0, 0);
	send_seq(&cl2, 1, 0);
	xpc_connection_send_barrier(cl2.peer, ^{
		barrier_runs++;
		xpc_connection_cancel(barrier_client->peer);
	});
	send_seq(&cl2, 2, 0);
	CHECK(queued(cl2.peer) == 3);
	CHECK(client_take(&cl2) == 0);
	run();
	CHECK(barrier_runs == 7 && host.invalid == 2 && queued(cl2.peer) == 0);
	CHECK(client_take(&cl2) == 1 && cl2.remote->count == 0);
	CHECK(entry_of(cl2.remote_name) == NULL && entry_of(cl2.local_name) == NULL);
	CHECK(host.vm_live == 0);
}

static void
test_suspend_holds_sends(void)
{
	host.invalid = 0;
	struct client cl = connect_client(4);
	struct conn *c = CONN(cl.peer);
	xpc_connection_suspend(cl.peer);
	for (int i = 0; i < 3; i++) {
		send_seq(&cl, i, 0);
	}
	run();
	CHECK(cl.remote->count == 0 && queued(cl.peer) == 3 && !cl.remote->sp_armed);
	xpc_connection_suspend(cl.peer);
	xpc_connection_resume(cl.peer);
	run();
	CHECK(cl.remote->count == 0 && queued(cl.peer) == 3);
	xpc_connection_resume(cl.peer);
	CHECK(cl.remote->count == 3 && queued(cl.peer) == 0 && c->sendq_dest == MACH_PORT_NULL);
	for (int i = 0; i < 3; i++) {
		CHECK(client_take(&cl) == i);
	}
	send_seq(&cl, 3, 0);
	CHECK(client_take(&cl) == 3);
	CHECK(host.vm_live == 0);
	finish(&cl);
	CHECK(host.invalid == 1);
}

static void
test_client_activation(void)
{
	host.service = port_new(false, 4);
	host.client_invalid = 0;
	host.interrupted = 0;
	host.replies = 0;
	host.reply_errors = 0;
	host.lookups = 0;
	barrier_runs = 0;
	xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
	xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
		if (e == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.interrupted++;
		} else if (e == XPC_ERROR_CONNECTION_INVALID) {
			host.client_invalid++;
		}
	});
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	xpc_connection_send_barrier(x, ^{
		barrier_runs++;
	});
	client_send(x, 1, 100000);
	xpc_object_t m = seq_message(2, 0);
	xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
		if (xpc_get_type(r) == XPC_TYPE_DICTIONARY && xpc_dictionary_get_int64(r, "seq") == 2) {
			host.replies++;
		} else {
			host.reply_errors++;
		}
	});
	xpc_release(m);
	run();
	CHECK(host.lookups == 0 && !MACH_PORT_VALID(c->remote) && host.service->count == 0);
	CHECK(queued(x) == 4 && barrier_runs == 0);
	xpc_connection_resume(x);
	CHECK(host.lookups == 1 && host.service->count == 1);
	CHECK(port_take(host.service, false) == 0);
	run();
	struct kport *rp = client_remote(x);
	CHECK(barrier_runs == 1 && rp != NULL);
	if (rp != NULL) {
		CHECK(rp->count == 2 && queued(x) == 0);
		CHECK(port_take(rp, false) == 1 && port_take(rp, true) == 2);
	}
	run();
	CHECK(host.replies == 1 && host.reply_errors == 0);
	CHECK(host.lookups == 1 && host.interrupted == 0 && c->sendq_dest == MACH_PORT_NULL);
	mach_port_t rname = c->remote;
	CHECK(NAME(rname).send == 1);
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL);
	xpc_release(x);

	host.lookups = 0;
	x = client_new();
	c = CONN(x);
	CHECK(host.lookups == 1 && MACH_PORT_VALID(c->service) && MACH_PORT_VALID(c->remote));
	CHECK(host.service->count == 0);
	client_send(x, 3, 0);
	CHECK(host.lookups == 1 && port_take(host.service, false) == 3);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(entry_of(host.service_name) == NULL);
	port_destroy(host.service);

	host.service = port_new(false, 4);
	host.client_invalid = 0;
	host.logged = 0;
	barrier_runs = 0;
	x = xpc_connection_create_mach_service("svc", NULL, 0);
	client_send(x, 0, 0);
	xpc_connection_send_barrier(x, ^{
		barrier_runs++;
	});
	client_send(x, 1, 0);
	client_send(x, 2, 0);
	host.fail_send = MACH_SEND_TOO_LARGE;
	xpc_connection_resume(x);
	CHECK(host.fail_send == KERN_SUCCESS && host.logged == 1 && host.service->count == 0);
	CHECK(barrier_runs == 0 && queued(x) == 2);
	run();
	CHECK(barrier_runs == 1 && port_take(host.service, false) == 1);
	run();
	rp = client_remote(x);
	CHECK(rp != NULL && rp->count == 1);
	if (rp != NULL) {
		CHECK(port_take(rp, false) == 2);
	}
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(entry_of(host.service_name) == NULL);
	port_destroy(host.service);

	host.client_invalid = 0;
	x = client_new();
	CHECK(CONN(x)->cancelled && host.lookups == 2);
	run();
	CHECK(host.client_invalid == 1);
	xpc_release(x);
	CHECK(host.vm_live == 0);
}

static void
test_peer_target(void)
{
	mach_port_t saved = host.listener;
	dispatch_queue_t target = dispatch_queue_create("target", NULL);
	xpc_connection_t l = xpc_connection_create_mach_service("svc3", target,
	    XPC_CONNECTION_MACH_SERVICE_LISTENER);
	static xpc_connection_t peer;
	peer = NULL;
	xpc_connection_set_event_handler(l, ^(xpc_object_t e) {
		if (xpc_get_type(e) == XPC_TYPE_CONNECTION) {
			peer = (xpc_connection_t)xpc_retain(e);
			xpc_connection_resume(e);
		}
	});
	xpc_connection_resume(l);
	run();
	struct client w = connect_begin(4);
	run();
	CHECK(CONN(l)->target == target && CONN(l)->q->target == target);
	CHECK(peer != NULL);
	if (peer != NULL) {
		CHECK(CONN(peer)->target == NULL && CONN(peer)->q->target == NULL);
		xpc_connection_cancel(peer);
		xpc_release(peer);
	}
	xpc_connection_cancel(l);
	run();
	CHECK(entry_of(w.local_name) == NULL && entry_of(w.remote_name) == NULL);
	xpc_release(l);
	dispatch_release(target);
	host.listener = saved;
}

static void
test_reply_body_error(void)
{
	host.service = port_new(false, 4);
	host.replies = 0;
	host.reply_errors = 0;
	xpc_connection_t x = client_new();
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	xpc_object_t m = seq_message(1, 0);
	xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
		if (r == XPC_ERROR_CONNECTION_INTERRUPTED) {
			host.reply_errors++;
		} else {
			host.replies++;
		}
	});
	xpc_release(m);
	struct pending *p = CONN(x)->pending;
	CHECK(p != NULL);
	if (p != NULL) {
		host.body_error = NAME(p->port).port;
		CHECK(port_take(client_remote(x), true) == 1);
		run();
	}
	CHECK(host.body_error == NULL && host.replies == 0 && host.reply_errors == 1);
	CHECK(CONN(x)->pending == NULL && live_once_rights() == 0);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(host.vm_live == 0);
	port_destroy(host.service);
}

static void
test_held_handshake_ports(void)
{
	host.service = port_new(false, 1);
	host.interrupted = 0;
	xpc_connection_t x = client_new();
	service_fill();
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);
	xpc_object_t m = ports_message(0, carried, 1);
	xpc_connection_send_message(x, m);
	xpc_release(m);
	CHECK(queued(x) == 1 && NAME(carried).send == 2);
	CHECK(port_take(host.service, false) == -5);
	run();
	CHECK(queued(x) == 0 && port_take(host.service, false) == 0);
	CHECK(NAME(carried).send == 1 && host.interrupted == 0);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL && host.vm_live == 0);
	port_destroy(host.service);
}

static xpc_object_t drained_request;

static void
test_reply_while_draining(void)
{
	for (int pass = 0; pass < 4; pass++) {
		host.service = port_new(false, 4);
		host.logged = 0;
		host.replies = 0;
		host.reply_errors = 0;
		host.lookups = 0;
		barrier_runs = 0;
		drained_request = NULL;
		xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
		xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
			if (xpc_get_type(e) == XPC_TYPE_DICTIONARY && drained_request == NULL) {
				drained_request = xpc_retain(e);
			}
		});
		xpc_connection_resume(x);
		struct conn *c = CONN(x);
		client_send(x, 0, 0);
		CHECK(port_take(host.service, false) == 0);
		run();
		struct client server = { .local = NAME(c->local).port, .local_name = c->local };
		struct kport *answer = port_new(false, 4);
		xpc_object_t m = seq_message(5, 0);
		request_from(&server, m, answer);
		xpc_release(m);
		run();
		CHECK(drained_request != NULL);
		if (drained_request == NULL) {
			continue;
		}
		xpc_object_t r = xpc_dictionary_create_reply(drained_request);
		xpc_dictionary_set_int64(r, "seq", 50);
		port_destroy(client_remote(x));
		notify_receive();
		CHECK(c->draining);
		if (pass >= 2) {
			port_destroy(host.service);
			if (pass == 2) {
				xpc_connection_send_message_with_reply(x, r, NULL, ^(xpc_object_t e) {
					if (e == XPC_ERROR_CONNECTION_INVALID) {
						host.reply_errors++;
					}
				});
			} else {
				xpc_connection_send_barrier(x, ^{
					barrier_runs++;
				});
			}
			xpc_release(r);
			run();
			CHECK(host.lookups == 1 && !c->cancelled && !MACH_PORT_VALID(c->remote));
			CHECK(queued(x) == 0 && barrier_runs == (pass == 3));
			CHECK((answer_head(answer, XPC_WIRE_REPLY) != NULL) == (pass == 2));
			if (answer->head != NULL) {
				kmsg_free(kq_take(answer));
			}
			xpc_connection_cancel(x);
			run();
			CHECK(host.reply_errors == (pass == 2));
			xpc_release(drained_request);
			xpc_release(x);
			free(answer);
			CHECK(host.vm_live == 0 && live_once_rights() == 0);
			continue;
		}
		client_send(x, 1, 0);
		xpc_connection_send_message_with_reply(x, r, NULL, ^(xpc_object_t e) {
			if (e == XPC_ERROR_CONNECTION_INVALID) {
				host.reply_errors++;
			}
		});
		xpc_release(r);
		client_send(x, 2, 0);
		CHECK(queued(x) == 3 && answer->count == 0);
		if (pass == 1) {
			host.fail_send = MACH_SEND_TOO_LARGE;
		}
		run();
		CHECK(answer_head(answer, XPC_WIRE_REPLY) != NULL);
		if (answer->head != NULL) {
			kmsg_free(kq_take(answer));
		}
		CHECK(port_take(host.service, false) == (pass == 0 ? 1 : 2));
		CHECK(host.logged == pass && queued(x) == 0);
		if (pass == 0) {
			CHECK(port_take(client_remote(x), false) == 2);
		}
		xpc_connection_cancel(x);
		run();
		CHECK(host.reply_errors == 1);
		xpc_release(drained_request);
		xpc_release(x);
		free(answer);
		CHECK(host.vm_live == 0 && live_once_rights() == 0);
		port_destroy(host.service);
	}
}

static xpc_connection_t lookup_client;

static void
lookup_cancel(void)
{
	xpc_connection_cancel(lookup_client);
}

static void
lookup_send(void)
{
	client_send(lookup_client, 1, 0);
	CHECK(queued(lookup_client) == 2 && CONN(lookup_client)->connecting);
}

static void
lookup_barrier(void)
{
	xpc_connection_send_barrier(lookup_client, ^{
		barrier_runs++;
	});
	CHECK(queued(lookup_client) == 2 && CONN(lookup_client)->connecting);
}

static void
test_lookup_unlocked(void)
{
	for (int path = 0; path < 2; path++) {
		host.service = port_new(false, 4);
		host.lookups = 0;
		barrier_runs = 0;
		xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
		lookup_client = x;
		if (path == 1) {
			xpc_connection_resume(x);
			client_send(x, -1, 0);
			CHECK(port_take(host.service, false) == -1);
			run();
			port_destroy(client_remote(x));
			run();
		}
		host.during_lookup = lookup_barrier;
		client_send(x, 0, 0);
		if (path == 0) {
			xpc_connection_resume(x);
		}
		CHECK(host.during_lookup == NULL && barrier_runs == 0);
		CHECK(port_take(host.service, false) == 0);
		run();
		CHECK(barrier_runs == 1 && !CONN(x)->cancelled && host.lookups == path + 1);
		xpc_connection_cancel(x);
		run();
		xpc_release(x);
		CHECK(live_once_rights() == 0 && host.vm_live == 0);
		port_destroy(host.service);
	}

	for (int pass = 0; pass < 6; pass++) {
		bool cancel = pass % 2 == 0;
		int path = pass / 2;
		host.service = port_new(false, 4);
		host.client_invalid = 0;
		host.interrupted = 0;
		host.reply_errors = 0;
		host.lookups = 0;
		xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
		xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
			if (e == XPC_ERROR_CONNECTION_INTERRUPTED) {
				host.interrupted++;
			} else if (e == XPC_ERROR_CONNECTION_INVALID) {
				host.client_invalid++;
			}
		});
		struct conn *c = CONN(x);
		lookup_client = x;
		if (path > 0) {
			xpc_connection_resume(x);
			client_send(x, -1, 0);
			CHECK(port_take(host.service, false) == -1);
			run();
			port_destroy(client_remote(x));
			if (path == 2) {
				notify_receive();
			} else {
				run();
				CHECK(host.interrupted == 1 && !c->draining);
			}
		}
		xpc_object_t m = seq_message(0, 0);
		host.during_lookup = cancel ? lookup_cancel : lookup_send;
		if (path == 1) {
			xpc_connection_send_message(x, m);
		} else {
			xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
				if (r == XPC_ERROR_CONNECTION_INVALID) {
					host.reply_errors++;
				}
			});
		}
		xpc_release(m);
		if (path == 0) {
			xpc_connection_resume(x);
		} else if (path == 2) {
			run();
		}
		CHECK(host.during_lookup == NULL && !c->connecting);
		CHECK(host.lookups == (path == 0 ? 1 : 2));
		if (cancel) {
			CHECK(c->cancelled && host.service->count == 0 && queued(x) == 0);
			CHECK(entry_of(host.service_name) == NULL);
		} else {
			CHECK(!c->cancelled && port_take(host.service, false) == 0);
			CHECK(port_take(client_remote(x), false) == 1);
		}
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && host.interrupted == (path == 0 ? 0 : 1));
		CHECK(host.reply_errors == (path == 1 ? 0 : 1));
		CHECK(live_once_rights() == 0 && entry_of(host.service_name) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static mach_msg_id_t
head_id(struct kport *p)
{
	return p->head != NULL ? p->head->m->msgh_id : 0;
}

static mach_msg_size_t
head_ports(struct kport *p)
{
	mach_msg_size_t n = 0;
	if (p->head != NULL) {
		ports_in(p->head->m, &n);
	}
	return n;
}

static int sent_after_cancel;

static void
count_sent(void)
{
	sent_after_cancel++;
}

static void
cancel_then_count(void)
{
	cancel_now();
	host.after_send = count_sent;
}

static void
cancel_second(void)
{
	sent_after_cancel = 0;
	host.before_send = cancel_then_count;
}

static void
test_full_handshake(void)
{
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);
	for (int pass = 0; pass < 8; pass++) {
		unsigned n = pass == 0 ? 16381 : pass == 1 ? 16382 : 16383;
		host.service = port_new(false, 4);
		host.interrupted = 0;
		host.client_invalid = 0;
		host.logged = 0;
		xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
		xpc_connection_set_event_handler(x, ^(xpc_object_t e) {
			if (e == XPC_ERROR_CONNECTION_INTERRUPTED) {
				host.interrupted++;
			} else if (e == XPC_ERROR_CONNECTION_INVALID) {
				host.client_invalid++;
			}
		});
		if (pass != 3 && pass != 4 && pass != 7) {
			xpc_connection_resume(x);
		}
		if (pass == 4) {
			client_send(x, 0, 0);
		}
		if (pass == 5) {
			host.fail_send = MACH_SEND_TOO_LARGE;
		}
		if (pass == 6 || pass == 7) {
			cancel_target = x;
			host.before_send = cancel_second;
		}
		xpc_object_t m = ports_message(1, carried, n);
		xpc_connection_send_message(x, m);
		xpc_release(m);
		if (pass != 7) {
			client_send(x, 2, 0);
		}
		if (pass == 4) {
			host.fail_send = MACH_SEND_TOO_LARGE;
		}
		if (pass == 3 || pass == 4 || pass == 7) {
			xpc_connection_resume(x);
		}
		run();
		struct conn *c = CONN(x);
		if (pass == 6 || pass == 7) {
			CHECK(c->cancelled && host.before_send == NULL);
			CHECK(head_id(host.service) == XPC_WIRE_CONNECT && port_take(host.service, false) == 0);
			CHECK(sent_after_cancel == 1);
			xpc_release(x);
			port_destroy(host.service);
			continue;
		}
		if (pass == 5) {
			CHECK(host.interrupted == 1 && queued(x) == 0 && !c->cancelled && host.logged == 0);
			CHECK(head_id(host.service) == XPC_WIRE_MESSAGE && port_take(host.service, false) == 2);
		} else if (pass == 0) {
			CHECK(head_id(host.service) == XPC_WIRE_MESSAGE && head_ports(host.service) == 16383);
			CHECK(port_take(host.service, false) == 1);
			CHECK(port_take(client_remote(x), false) == 2);
		} else {
			CHECK(host.logged == (pass == 4) && host.interrupted == 0);
			CHECK(head_id(host.service) == XPC_WIRE_CONNECT && head_ports(host.service) == 2);
			CHECK(host.service->count == 1 && port_take(host.service, false) == 0);
			struct kport *rp = client_remote(x);
			CHECK(rp != NULL && head_id(rp) == XPC_WIRE_MESSAGE && head_ports(rp) == n);
			if (rp != NULL) {
				CHECK(port_take(rp, false) == 1);
				run();
				CHECK(port_take(rp, false) == 2 && rp->count == 0);
				CHECK(watching_death(rp));
				if (pass == 1) {
					port_destroy(rp);
					run();
					CHECK(host.interrupted == 1 && !c->cancelled);
				}
			}
		}
		CHECK(NAME(carried).send == 1 && !c->cancelled);
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(host.service_name) == NULL);
		CHECK(live_once_rights() == 0 && host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL);

	int peers = host.npeers;
	host.handled = 0;
	struct client cl = { 0 };
	mach_port_options_t lopts = { .flags = MPO_QLIMIT, .mpl = { .mpl_qlimit = 5 } };
	mach_port_construct(1, &lopts, 0, &cl.local_name);
	cl.local = NAME(cl.local_name).port;
	cl.remote = port_new(false, 4);
	cl.remote_name = name_new(cl.remote);
	NAME(cl.remote_name).send = 1;
	xpc_object_t empty = xpc_dictionary_create(NULL, NULL, 0);
	mach_msg_header_t *hs = client_message(empty, host.listener, cl.local_name, cl.remote_name);
	xpc_release(empty);
	hs->msgh_id = XPC_WIRE_CONNECT;
	kq_put(NAME(host.listener).port, hs, 1234, NULL, NULL);
	run();
	CHECK(host.npeers == peers + 1 && host.handled == 0);
	cl.peer = host.peers[peers];
	mach_port_t stray_recv, stray_send;
	mach_port_construct(1, &lopts, 0, &stray_recv);
	stray_send = name_new(port_new(false, 4));
	NAME(stray_send).send = 1;
	empty = xpc_dictionary_create(NULL, NULL, 0);
	hs = client_message(empty, cl.local_name, stray_recv, stray_send);
	xpc_release(empty);
	hs->msgh_id = XPC_WIRE_CONNECT;
	kq_put(cl.local, hs, 1234, NULL, NULL);
	run();
	CHECK(host.handled == 0 && cl.local->count == 0);
	CHECK(entry_of(stray_recv) == NULL && entry_of(stray_send) == NULL);
	xpc_object_t m = seq_message(3, 0);
	inject(&cl, m);
	xpc_release(m);
	run();
	CHECK(host.handled == 1);
	send_seq(&cl, 4, 0);
	CHECK(client_take(&cl) == 4);
	finish(&cl);
}

static void
test_connect_unprepared(void)
{
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_t carried;
	mach_port_construct(1, &opts, 0, &carried);
	for (int pass = 0; pass < 3; pass++) {
		host.service = port_new(false, 4);
		host.interrupted = 0;
		host.logged = 0;
		host.malloc_failed = 0;
		host.too_large = 0;
		xpc_connection_t x = xpc_connection_create_mach_service("svc", NULL, 0);
		struct conn *c = CONN(x);
		if (pass == 0) {
			xpc_connection_resume(x);
		}
		if (pass == 2) {
			client_send(x, 0, 0);
		}
		xpc_object_t m = ports_message(1, carried, XPC_WIRE_PORTS_MAX);
		host.fail_malloc = 1000;
		host.fail_malloc_skip = pass == 0 ? 1 : 0;
		if (pass != 0) {
			host.fail_malloc = 0;
		}
		xpc_connection_send_message(x, m);
		xpc_release(m);
		client_send(x, 2, 0);
		if (pass != 0) {
			host.fail_malloc = 1000;
			if (pass == 2) {
				host.fail_send = MACH_SEND_TOO_LARGE;
			}
			xpc_connection_resume(x);
		}
		run();
		CHECK(host.malloc_failed == 1 && host.fail_malloc == 0 && host.fail_malloc_skip == 0);
		CHECK(host.too_large == 0);
		CHECK(host.logged == (pass == 2 ? 2 : 1) && host.interrupted == 0 && !c->cancelled);
		CHECK(head_id(host.service) == XPC_WIRE_MESSAGE && port_take(host.service, false) == 2);
		CHECK(host.service->count == 0 && queued(x) == 0 && NAME(carried).send == 1);
		xpc_connection_cancel(x);
		run();
		xpc_release(x);
		CHECK(entry_of(host.service_name) == NULL && live_once_rights() == 0 && host.vm_live == 0);
		port_destroy(host.service);
	}
	mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, carried);
	CHECK(entry_of(carried) == NULL);
}

static xpc_pipe_t trap_pipe;

static void
pipe_too_many(void)
{
	xpc_object_t m = ports_message(1, trap_port, XPC_WIRE_PORTS_MAX + 1);
	xpc_pipe_simpleroutine(trap_pipe, m);
}

static void
pipe_too_long(void)
{
	char small[16] = { 0 };
	xpc_object_t m = seq_message(1, 0);
	xpc_object_t d = xpc_data_create(small, sizeof(small));
	XO(d)->buf.len = (size_t)UINT32_MAX + 1;
	xpc_dictionary_set_value(m, "d", d);
	xpc_object_t r = NULL;
	xpc_pipe_routine(trap_pipe, m, &r);
}

static void
test_pipes(void)
{
	struct kport *srv = port_new(false, 4);
	mach_port_t name = name_new(srv);
	NAME(name).send = 1;
	xpc_pipe_t p = xpc_pipe_create_from_port(name, 0);
	xpc_object_t m = seq_message(1, 0);
	CHECK(xpc_pipe_simpleroutine(p, m) == 0 && srv->count == 1);
	xpc_release(m);
	CHECK(port_take(srv, false) == 1);
	mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
	mach_port_construct(1, &opts, 0, &trap_port);
	trap_pipe = p;
	CHECK(child_traps(pipe_too_many));
	CHECK(child_traps(pipe_too_long));
	CHECK(srv->count == 0);
	xpc_release(p);
	CHECK(entry_of(name) == NULL);
	mach_port_mod_refs(1, trap_port, MACH_PORT_RIGHT_RECEIVE, -1);
	mach_port_deallocate(1, trap_port);
	free(srv);

	mach_port_options_t qopts = { .flags = MPO_QLIMIT, .mpl = { .mpl_qlimit = 4 } };
	mach_port_t rcv, stray_recv;
	mach_port_construct(1, &qopts, 0, &rcv);
	mach_port_construct(1, &qopts, 0, &stray_recv);
	mach_port_t stray_send = name_new(port_new(false, 4));
	NAME(stray_send).send = 1;
	xpc_object_t empty = xpc_dictionary_create(NULL, NULL, 0);
	mach_msg_header_t *hs = client_message(empty, rcv, stray_recv, stray_send);
	xpc_release(empty);
	hs->msgh_id = XPC_WIRE_CONNECT;
	kq_put(NAME(rcv).port, hs, 1234, NULL, NULL);
	m = seq_message(2, 0);
	kq_put(NAME(rcv).port, client_message(m, rcv, MACH_PORT_NULL, MACH_PORT_NULL), 1234, NULL, NULL);
	xpc_release(m);
	xpc_object_t got = (xpc_object_t)1;
	CHECK(xpc_pipe_try_receive(rcv, &got, NULL, NULL, 0, 0) == EINVAL && got == NULL);
	CHECK(entry_of(stray_recv) == NULL && entry_of(stray_send) == NULL);
	CHECK(xpc_pipe_receive(rcv, &got) == 0 && got != NULL);
	CHECK(xpc_dictionary_get_int64(got, "seq") == 2);
	xpc_release(got);
	mach_port_mod_refs(1, rcv, MACH_PORT_RIGHT_RECEIVE, -1);
	CHECK(entry_of(rcv) == NULL && host.vm_live == 0);
}

static void
test_hs_copy_fails(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	host.logged = 0;
	host.malloc_failed = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	CHECK(MACH_PORT_VALID(c->service));
	xpc_object_t m = seq_message(0, 3000);
	host.fail_malloc = 3000;
	xpc_connection_send_message(x, m);
	xpc_release(m);
	CHECK(host.malloc_failed == 1 && host.logged == 1 && queued(x) == 0);
	CHECK(MACH_PORT_VALID(c->service) && client_remote(x) != NULL);
	client_send(x, 1, 0);
	CHECK(port_take(host.service, false) == 1);
	run();
	CHECK(host.interrupted == 0 && !c->cancelled);
	xpc_connection_cancel(x);
	run();
	xpc_release(x);
	CHECK(live_once_rights() == 0 && host.vm_live == 0);
	port_destroy(host.service);
}

static void
just_run(void)
{
	run();
}

static void
test_drain_lookup_fails(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	xpc_connection_t x = client_new();
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	run();
	port_destroy(client_remote(x));
	port_destroy(host.service);
	notify_receive();
	CHECK(CONN(x)->draining);
	host.blocked = just_run;
	xpc_object_t m = seq_message(1, 0);
	xpc_object_t r = xpc_connection_send_message_with_reply_sync(x, m);
	xpc_release(m);
	host.blocked = NULL;
	CHECK(r == XPC_ERROR_CONNECTION_INVALID && CONN(x)->cancelled);
	run();
	CHECK(host.interrupted == 1 && host.client_invalid == 1);
	CHECK(CONN(x)->pending == NULL && live_once_rights() == 0 && host.vm_live == 0);
	xpc_release(x);
}

static void
test_client_stale_sends(void)
{
	host.wrap = true;
	for (int pass = 0; pass < 4; pass++) {
		host.reply_errors = 0;
		host.service = port_new(false, 4);
		host.interrupted = 0;
		host.client_invalid = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		client_send(x, 0, 0);
		CHECK(port_take(host.service, false) == 0);
		mach_port_t rname = c->remote, lname = c->local;
		struct kport *rp = client_remote(x);
		rp->qlimit = 1;
		client_send(x, 1, 0);
		stale_client = x;
		stale_remote = rname;
		if (pass == 0) {
			host.timed_out = stale_reconnect;
			client_send(x, 2, 0);
		} else {
			client_send(x, 2, 0);
			client_send(x, 3, 0);
			CHECK(queued(x) == 2 && rp->sp_armed);
			CHECK(port_take(rp, false) == 1);
			if (pass == 1) {
				host.timed_out = stale_reconnect;
			} else if (pass == 2) {
				host.before_send = stale_reconnect;
			} else {
				host.before_send = stale_reconnect_reply;
			}
			run();
		}
		CHECK(host.timed_out == NULL && host.before_send == NULL);
		run();
		CHECK(host.interrupted == 1 && !c->cancelled);
		CHECK(host.reply_errors == (pass == 3));
		CHECK(MACH_PORT_VALID(c->remote) && c->remote != rname);
		CHECK(port_take(host.service, false) == 99);
		CHECK(queued(x) == 0 && host.service->count == 0);
		CHECK(entry_of(rname) == NULL && *note_find(rname) == NULL);
		mach_port_t r2 = c->remote;
		CHECK(NAME(r2).send == 1 && watching_death(NAME(r2).port));
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && host.interrupted == 1);
		CHECK(entry_of(r2) == NULL && entry_of(lname) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
	host.wrap = false;
}

static xpc_connection_t dying_client;

static void
remote_dies(void)
{
	port_destroy(NAME(CONN(dying_client)->remote).port);
}

static void
test_client_dead_at_watch(void)
{
	for (int pass = 0; pass < 2; pass++) {
		host.service = port_new(false, 1);
		host.interrupted = 0;
		host.client_invalid = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		dying_client = x;
		if (pass == 1) {
			service_fill();
		}
		host.after_send = remote_dies;
		client_send(x, 0, 0);
		mach_port_t rname = c->remote, lname = c->local;
		if (pass == 1) {
			CHECK(queued(x) == 1 && host.service->sp_armed);
			CHECK(port_take(host.service, false) == -5);
			while (host.service->count == 0 && step()) {
			}
		}
		CHECK(c->remote == rname && *note_find(rname) != NULL &&
		    (*note_find(rname))->requests == 1 && NAME(rname).send == 2);
		CHECK(host.after_send == NULL && queued(x) == 0);
		CHECK(port_take(host.service, false) == 0);
		run();
		CHECK(host.interrupted == 1 && !c->cancelled && !MACH_PORT_VALID(c->remote));
		CHECK(entry_of(rname) == NULL && *note_find(rname) == NULL);
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(lname) == NULL);
		CHECK(entry_of(host.service_name) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static void
test_stale_watch(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	client_send(x, 0, 0);
	CHECK(port_take(host.service, false) == 0);
	mach_port_t rname = c->remote;
	struct kport *rp = NAME(rname).port;
	CHECK(c->watch == rname && watching_death(rp));
	rp->qlimit = 1;
	client_send(x, 1, 0);
	client_send(x, 2, 0);
	CHECK(queued(x) == 1 && c->sp_armed);
	notified(XO(x), MACH_NOTIFY_DEAD_NAME, host.service_name);
	notified(XO(x), MACH_NOTIFY_SEND_POSSIBLE, host.service_name);
	CHECK(host.interrupted == 0 && c->remote == rname && queued(x) == 1 && c->sp_armed);
	CHECK(port_take(rp, false) == 1);
	run();
	CHECK(port_take(rp, false) == 2 && queued(x) == 0);
	run();
	CHECK(host.interrupted == 0 && watching_death(rp));
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static void
test_client_bad_handshake(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	host.lookups = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	xpc_object_t bad = seq_message(0, 0);
	xpc_dictionary_set_value(bad, "connection", x);
	xpc_connection_send_message(x, bad);
	xpc_release(bad);
	mach_port_t rname = c->remote;
	CHECK(MACH_PORT_VALID(c->service) && NAME(rname).recv && host.service->count == 0);
	host.fail_send = MACH_SEND_TOO_LARGE;
	host.logged = 0;
	client_send(x, 1, 0);
	CHECK(host.logged == 1);
	CHECK(MACH_PORT_VALID(c->service) && c->remote == rname && NAME(rname).recv);
	CHECK(host.service->count == 0 && queued(x) == 0);
	client_send(x, 2, 0);
	CHECK(port_take(host.service, false) == 2);
	CHECK(c->remote == rname && !NAME(rname).recv && host.lookups == 1);
	run();
	CHECK(host.interrupted == 0 && !c->cancelled);
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL);
	CHECK(entry_of(host.service_name) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static xpc_connection_t racing_client;

static void
racing_sends(void)
{
	client_remote(racing_client)->qlimit = 1;
	client_send(racing_client, 1, 0);
	client_send(racing_client, 2, 0);
}

static void
racing_reject(void)
{
	racing_sends();
	host.fail_send = MACH_SEND_TOO_LARGE;
}

static int racing_seq;

static void
racing_second(void)
{
	int timeouts = host.timeouts;
	client_send(racing_client, racing_seq, 0);
	CHECK(host.timeouts == timeouts && queued(racing_client) == 1);
}

static void
test_client_admission(void)
{
	for (int pass = 0; pass < 3; pass++) {
		host.service = port_new(false, pass == 0 ? 1 : 4);
		host.interrupted = 0;
		host.client_invalid = 0;
		host.lookups = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		racing_client = x;
		if (pass == 0) {
			service_fill();
		}
		host.before_send = pass == 0 ? racing_sends : pass == 1 ? racing_reject : NULL;
		client_send(x, 0, 0);
		mach_port_t rname = c->remote, lname = c->local;
		struct kport *rp = NAME(rname).port;
		int first = 0;
		if (pass == 0) {
			CHECK(queued(x) == 3 && MACH_PORT_VALID(c->sendq->service));
			CHECK(host.service->sp_armed && NAME(rname).recv);
			CHECK(port_take(host.service, false) == -5);
			run();
		} else if (pass == 1) {
			first = 1;
		} else {
			rp->qlimit = 2;
			racing_seq = 11;
			host.before_send = racing_second;
			client_send(x, 10, 0);
			CHECK(rp->count == 2 && queued(x) == 0);
			racing_seq = 13;
			host.before_send = racing_second;
			client_send(x, 12, 0);
			CHECK(queued(x) == 2 && rp->sp_armed);
		}
		CHECK(host.before_send == NULL && host.fail_send == KERN_SUCCESS);
		CHECK(port_take(host.service, false) == first);
		CHECK(host.service->count == 0 && !MACH_PORT_VALID(c->service));
		CHECK(!NAME(rname).recv && !rp->ours);
		int next = pass == 2 ? 10 : first + 1, last = pass == 2 ? 13 : 2;
		bool ordered = true;
		for (int guard = 0; guard < 10 && next <= last; guard++) {
			while (rp->count > 0) {
				ordered = ordered && port_take(rp, false) == next;
				next++;
			}
			run();
		}
		CHECK(next == last + 1 && ordered);
		CHECK(queued(x) == 0 && c->sendq_dest == MACH_PORT_NULL && watching_death(rp));
		CHECK(host.interrupted == 0 && !c->cancelled && host.lookups == 1);
		CHECK(NAME(rname).send == 1);
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(rname) == NULL && entry_of(lname) == NULL);
		CHECK(entry_of(host.service_name) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static void
racing_reset(void)
{
	if (racing_seq > 0) {
		client_send(racing_client, racing_seq, 0);
	}
	port_destroy(client_remote(racing_client));
	notify_receive();
}

static void
test_send_windows(void)
{
	for (int pass = 0; pass < 4; pass++) {
		host.service = port_new(false, pass >= 2 ? 1 : 4);
		host.interrupted = 0;
		host.client_invalid = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		racing_client = x;
		mach_port_t carried = MACH_PORT_NULL;
		mach_port_options_t opts = { .flags = MPO_INSERT_SEND_RIGHT };
		mach_port_construct(1, &opts, 0, &carried);
		if (pass >= 2) {
			service_fill();
		}
		xpc_object_t m = seq_message(0, 0);
		if (pass >= 2) {
			xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, carried);
			mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_SEND, 1);
			xpc_dictionary_set_value(m, "port", port);
			xpc_release(port);
		}
		xpc_connection_send_message(x, m);
		mach_port_t rname = c->remote, lname = c->local;
		if (pass < 2) {
			CHECK(port_take(host.service, false) == 0);
			run();
			struct kport *rp = NAME(rname).port;
			if (pass == 0) {
				racing_seq = 1;
				host.before_send = racing_reset;
				client_send(x, 2, 0);
			} else {
				rp->qlimit = 1;
				xpc_object_t port = _xpc_port_create(XPC_TYPE_MACH_SEND, carried);
				mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_SEND, 1);
				xpc_dictionary_set_value(m, "port", port);
				xpc_release(port);
				client_send(x, 1, 0);
				xpc_connection_send_message(x, m);
				client_send(x, 3, 0);
				CHECK(queued(x) == 2 && rp->sp_armed);
				racing_seq = 0;
				host.on_deallocate = racing_reset;
				CHECK(port_take(rp, false) == 1);
				run();
				CHECK(host.on_deallocate == NULL);
			}
			CHECK(host.before_send == NULL);
		} else {
			CHECK(queued(x) == 1 && NAME(rname).recv);
			CHECK(port_take(host.service, false) == -5);
			if (pass == 2) {
				host.fail_send = MACH_SEND_INVALID_DEST;
			} else {
				host.bad_right = carried;
			}
			run();
			host.bad_right = MACH_PORT_NULL;
			CHECK(host.fail_send == KERN_SUCCESS && host.service->count == 0);
		}
		run();
		CHECK(host.interrupted == 1 && !c->cancelled && queued(x) == 0);
		CHECK(!MACH_PORT_VALID(c->remote) && !MACH_PORT_VALID(c->service));
		CHECK(entry_of(rname) == NULL && *note_find(rname) == NULL);
		xpc_release(m);
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(lname) == NULL);
		CHECK(entry_of(host.service_name) == NULL);
		CHECK(NAME(carried).send == 1);
		mach_port_mod_refs(1, carried, MACH_PORT_RIGHT_RECEIVE, -1);
		mach_port_deallocate(1, carried);
		CHECK(entry_of(carried) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static mach_port_t seen_remote;
static struct kport *seen_port;

static void
remote_seen(void)
{
	seen_remote = CONN(racing_client)->remote;
	seen_port = NAME(seen_remote).port;
}

static void
test_client_reply_failures(void)
{
	static const kern_return_t codes[] = {
		MACH_SEND_INVALID_OPTIONS, MACH_SEND_MSG_FILTERED, MACH_SEND_INVALID_RIGHT,
	};
	int once = live_once_rights();
	for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
		bool body = codes[i] == MACH_SEND_INVALID_RIGHT;
		host.service = port_new(false, 4);
		host.interrupted = 0;
		host.client_invalid = 0;
		host.replies = 0;
		host.reply_errors = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		xpc_object_t m = seq_message(0, 0);
		racing_client = x;
		host.before_send = remote_seen;
		host.fail_send = codes[i];
		xpc_connection_send_message_with_reply(x, m, NULL, ^(xpc_object_t r) {
			if (r == XPC_ERROR_CONNECTION_INTERRUPTED) {
				host.reply_errors++;
			} else if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) {
				host.replies++;
			}
		});
		xpc_release(m);
		mach_port_t rname = seen_remote, lname = c->local;
		struct kport *rp = seen_port;
		CHECK(host.fail_send == KERN_SUCCESS && host.service->count == 0);
		run();
		CHECK(host.reply_errors == 1 && host.replies == 0 && c->pending == NULL);
		CHECK(live_once_rights() == once && queued(x) == 0);
		if (body) {
			CHECK(!rp->alive && host.interrupted == 1);
			CHECK(!MACH_PORT_VALID(c->remote) && !MACH_PORT_VALID(c->service));
			CHECK(entry_of(rname) == NULL);
		} else {
			CHECK(rp->alive && host.interrupted == 0);
			CHECK(c->remote == rname && MACH_PORT_VALID(c->service) && NAME(rname).recv);
		}
		client_send(x, 1, 0);
		CHECK(port_take(host.service, false) == 1);
		run();
		CHECK(host.interrupted == (body ? 1 : 0) && watching_death(client_remote(x)));
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(rname) == NULL && entry_of(lname) == NULL);
		CHECK(entry_of(host.service_name) == NULL && live_once_rights() == once);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

static void
racing_follow(void)
{
	client_send(racing_client, 1, 0);
}

static void
test_client_watch_fails(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	racing_client = x;
	host.before_send = racing_follow;
	host.fail_request = KERN_RESOURCE_SHORTAGE;
	client_send(x, 0, 0);
	CHECK(host.before_send == NULL && host.fail_request == KERN_SUCCESS);
	CHECK(port_take(host.service, false) == 0);
	run();
	CHECK(host.interrupted == 1 && !c->cancelled && queued(x) == 0);
	CHECK(!MACH_PORT_VALID(c->remote) && c->watch == MACH_PORT_NULL);
	client_send(x, 2, 0);
	CHECK(port_take(host.service, false) == 2);
	run();
	CHECK(host.interrupted == 1 && watching_death(client_remote(x)));
	mach_port_t rname = c->remote, lname = c->local;
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL && entry_of(lname) == NULL);
	CHECK(entry_of(host.service_name) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static void
handshake_unlocked(os_unfair_lock *lock)
{
	struct conn *c = CONN(racing_client);
	if (lock == &c->lock && MACH_PORT_VALID(c->remote) && !MACH_PORT_VALID(c->service)) {
		on_unlock = NULL;
		client_send(racing_client, 1, 0);
	}
}

static void
test_client_admission_window(void)
{
	host.service = port_new(false, 4);
	host.interrupted = 0;
	host.client_invalid = 0;
	xpc_connection_t x = client_new();
	struct conn *c = CONN(x);
	racing_client = x;
	on_unlock = handshake_unlocked;
	client_send(x, 0, 0);
	CHECK(on_unlock == NULL && host.self_sends == 0);
	mach_port_t rname = c->remote, lname = c->local;
	struct kport *rp = NAME(rname).port;
	CHECK(port_take(host.service, false) == 0 && host.service->count == 0);
	CHECK(!NAME(rname).recv && !rp->ours && queued(x) == 0);
	CHECK(rp->count == 1 && port_take(rp, false) == 1);
	run();
	CHECK(host.interrupted == 0 && !c->cancelled && watching_death(rp));
	xpc_connection_cancel(x);
	run();
	CHECK(host.client_invalid == 1 && entry_of(rname) == NULL && entry_of(lname) == NULL);
	CHECK(entry_of(host.service_name) == NULL);
	CHECK(host.vm_live == 0);
	xpc_release(x);
	port_destroy(host.service);
}

static void
test_client_header_failures(void)
{
	static const kern_return_t header[] = {
		MACH_SEND_INVALID_REPLY, MACH_SEND_INVALID_OPTIONS, MACH_SEND_MSG_FILTERED,
	};
	for (size_t i = 0; i < sizeof(header) / sizeof(header[0]); i++) {
		host.service = port_new(false, 1);
		host.interrupted = 0;
		host.client_invalid = 0;
		xpc_connection_t x = client_new();
		struct conn *c = CONN(x);
		host.fail_send = header[i];
		client_send(x, 0, 0);
		mach_port_t rname = c->remote, lname = c->local;
		CHECK(host.fail_send == KERN_SUCCESS && host.service->count == 0);
		CHECK(MACH_PORT_VALID(c->service) && NAME(rname).recv && NAME(rname).send == 1);
		client_send(x, 1, 0);
		CHECK(port_take(host.service, false) == 1);
		CHECK(c->remote == rname && !NAME(rname).recv && !MACH_PORT_VALID(c->service));
		run();
		CHECK(host.interrupted == 0 && !c->cancelled);

		service_fill();
		port_destroy(NAME(rname).port);
		run();
		CHECK(host.interrupted == 1 && entry_of(rname) == NULL);
		client_send(x, 2, 0);
		rname = c->remote;
		CHECK(queued(x) == 1 && NAME(rname).recv);
		CHECK(port_take(host.service, false) == -5);
		host.fail_send = header[i];
		run();
		CHECK(host.fail_send == KERN_SUCCESS && host.interrupted == 1);
		CHECK(queued(x) == 0 && host.service->count == 0);
		CHECK(c->remote == rname && MACH_PORT_VALID(c->service) && NAME(rname).recv);
		client_send(x, 3, 0);
		CHECK(port_take(host.service, false) == 3 && !NAME(rname).recv);
		run();
		CHECK(host.interrupted == 1 && watching_death(NAME(rname).port));
		xpc_connection_cancel(x);
		run();
		CHECK(host.client_invalid == 1 && entry_of(lname) == NULL);
		CHECK(entry_of(host.service_name) == NULL);
		CHECK(host.vm_live == 0);
		xpc_release(x);
		port_destroy(host.service);
	}
}

int
main(void)
{
	alarm(120);
	test_note_buckets();
	start_listener();
	run();
	test_flow_control();
	test_spoofed_notifications();
	test_notify_while_held();
	test_gone_notifications();
	test_full_client();
	test_one_at_a_time();
	test_busy_client();
	test_replies_and_ports();
	test_large_messages();
	test_send_copy_fails();
	test_cancel_races();
	test_dead_port_in_queue();
	test_client_dies();
	test_rejected_handshakes();
	test_client_queue();
	test_client_sync();
	test_client_server_dies();
	test_server_dies_with_messages();
	test_peer_remote_dies();
	test_reply_while_draining();
	test_reply_body_error();
	test_held_handshake_ports();
	test_privileged_client();
	test_reply_with_reply();
	test_lookup_unlocked();
	test_drain_lookup_fails();
	test_hs_copy_fails();
	test_full_handshake();
	test_pipes();
	test_connect_unprepared();
	test_cancel_wakes_sync();
	test_bad_messages();
	test_send_barrier();
	test_suspend_holds_sends();
	test_client_activation();
	test_peer_target();
	test_client_stale_sends();
	test_client_dead_at_watch();
	test_stale_watch();
	test_client_watch_fails();
	test_client_bad_handshake();
	test_client_header_failures();
	test_client_reply_failures();
	test_client_admission();
	test_client_admission_window();
	test_send_windows();
	CHECK(host.errors == 0 && host.self_sends == 0);
	if (failures != 0) {
		fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	printf("conn_test: all passed\n");
	return 0;
}
