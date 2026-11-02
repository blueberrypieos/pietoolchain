/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <xpc/private.h>
#include <servers/bootstrap.h>
#include <vproc.h>
#include <vproc_priv.h>
#include <bootstrap_priv.h>
#include <reboot2.h>
#include <errno.h>
#include <mach/mig.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pie_bootstrap.h"

static void
get_bootstrap_port(void)
{
	if (task_get_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, &bootstrap_port) != KERN_SUCCESS) {
		bootstrap_port = MACH_PORT_NULL;
	}
}

void _libxpc_initializer(void) { get_bootstrap_port(); }
void xpc_atfork_prepare(void) { }
void xpc_atfork_parent(void) { }
void xpc_atfork_child(void) { get_bootstrap_port(); }

static kern_return_t
bs_rpc(mach_port_t bp, mach_msg_id_t id, const char *name, mach_port_t send, uint64_t arg,
    mach_port_t *port)
{
	union {
		pie_bs_request_t req;
		pie_bs_reply_buffer_t rep;
	} m;

	if (port != NULL) {
		*port = MACH_PORT_NULL;
	}
	if (!MACH_PORT_VALID(bp)) {
		return BOOTSTRAP_UNKNOWN_SERVICE;
	}
	mach_port_t reply = mig_get_reply_port();
	memset(&m, 0, sizeof(m));
	m.req.head.msgh_bits = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND,
	    MACH_MSG_TYPE_MAKE_SEND_ONCE, 0, MACH_MSGH_BITS_COMPLEX);
	m.req.head.msgh_size = sizeof(m.req);
	m.req.head.msgh_remote_port = bp;
	m.req.head.msgh_local_port = reply;
	m.req.head.msgh_id = id;
	m.req.body.msgh_descriptor_count = 1;
	m.req.port.name = send;
	m.req.port.disposition = MACH_MSG_TYPE_COPY_SEND;
	m.req.port.type = MACH_MSG_PORT_DESCRIPTOR;
	m.req.arg = arg;
	if (name != NULL) {
		strlcpy(m.req.name, name, sizeof(m.req.name));
	}

	kern_return_t kr = mach_msg(&m.req.head, MACH_SEND_MSG | MACH_RCV_MSG, sizeof(m.req),
	    sizeof(m.rep), reply, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if (kr != KERN_SUCCESS) {
		if (kr != MACH_SEND_INVALID_DEST && kr != MACH_SEND_INVALID_RIGHT) {
			mig_dealloc_reply_port(reply);
		}
		return kr == MACH_SEND_INVALID_DEST ? BOOTSTRAP_UNKNOWN_SERVICE : kr;
	}
	pie_bs_reply_t *r = &m.rep.reply;
	if (r->head.msgh_id != id + PIE_BS_REPLY || !(r->head.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
	    r->body.msgh_descriptor_count != 1) {
		mach_msg_destroy(&r->head);
		return MIG_REPLY_MISMATCH;
	}
	if (port != NULL) {
		*port = r->port.name;
	} else if (MACH_PORT_VALID(r->port.name)) {
		mach_port_deallocate(mach_task_self(), r->port.name);
	}
	return r->ret;
}

kern_return_t
bootstrap_look_up(mach_port_t bp, const name_t service_name, mach_port_t *sp)
{
	return bs_rpc(bp, PIE_BS_LOOK_UP, service_name, MACH_PORT_NULL, 0, sp);
}

kern_return_t
bootstrap_check_in(mach_port_t bp, const name_t service_name, mach_port_t *sp)
{
	return bs_rpc(bp, PIE_BS_CHECK_IN, service_name, MACH_PORT_NULL, 0, sp);
}

kern_return_t
bootstrap_look_up2(mach_port_t bp, const name_t service_name, mach_port_t *sp,
    pid_t target_pid, uint64_t flags)
{
	(void)target_pid;
	(void)flags;
	return bootstrap_look_up(bp, service_name, sp);
}

kern_return_t
bootstrap_look_up3(mach_port_t bp, const name_t service_name, mach_port_t *sp,
    pid_t target_pid, const uuid_t instance_id, uint64_t flags)
{
	(void)instance_id;
	return bootstrap_look_up2(bp, service_name, sp, target_pid, flags);
}

kern_return_t
bootstrap_look_up_per_user(mach_port_t bp, const name_t service_name, uid_t target_user,
    mach_port_t *sp)
{
	(void)target_user;
	return bootstrap_look_up(bp, service_name, sp);
}

kern_return_t
bootstrap_check_in2(mach_port_t bp, const name_t service_name, mach_port_t *sp, uint64_t flags)
{
	(void)flags;
	return bootstrap_check_in(bp, service_name, sp);
}

kern_return_t
bootstrap_check_in3(mach_port_t bp, const name_t service_name, mach_port_t *sp,
    uuid_t instance_id, uint64_t flags)
{
	(void)instance_id;
	return bootstrap_check_in2(bp, service_name, sp, flags);
}

kern_return_t
bootstrap_register(mach_port_t bp, name_t service_name, mach_port_t sp)
{
	return bs_rpc(bp, PIE_BS_REGISTER, service_name, sp, 0, NULL);
}

kern_return_t
bootstrap_register2(mach_port_t bp, name_t service_name, mach_port_t sp, uint64_t flags)
{
	(void)flags;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	return bootstrap_register(bp, service_name, sp);
#pragma clang diagnostic pop
}

kern_return_t
bootstrap_create_service(mach_port_t bp, name_t service_name, mach_port_t *sp)
{
	(void)bp;
	(void)service_name;
	*sp = MACH_PORT_NULL;
	return BOOTSTRAP_NOT_PRIVILEGED;
}

kern_return_t
bootstrap_parent(mach_port_t bp, mach_port_t *parent_port)
{
	*parent_port = bp;
	return BOOTSTRAP_SUCCESS;
}

kern_return_t
bootstrap_get_root(mach_port_t bp, mach_port_t *root)
{
	return bootstrap_parent(bp, root);
}

kern_return_t
bootstrap_status(mach_port_t bp, name_t service_name, bootstrap_status_t *service_active)
{
	mach_port_t port;
	kern_return_t kr = bootstrap_look_up(bp, service_name, &port);
	*service_active = kr == BOOTSTRAP_SUCCESS ? BOOTSTRAP_STATUS_ACTIVE : BOOTSTRAP_STATUS_INACTIVE;
	if (MACH_PORT_VALID(port)) {
		mach_port_deallocate(mach_task_self(), port);
	}
	return kr;
}

const char *
bootstrap_strerror(kern_return_t r)
{
	switch (r) {
	case BOOTSTRAP_SUCCESS:         return "Success";
	case BOOTSTRAP_NOT_PRIVILEGED:  return "Permission denied";
	case BOOTSTRAP_NAME_IN_USE:     return "Service name already exists";
	case BOOTSTRAP_UNKNOWN_SERVICE: return "Unknown service name";
	case BOOTSTRAP_SERVICE_ACTIVE:  return "Service already active";
	case BOOTSTRAP_BAD_COUNT:       return "Too many lookups were requested in one request";
	case BOOTSTRAP_NO_MEMORY:       return "Out of memory";
	case BOOTSTRAP_NO_CHILDREN:     return "No children";
	default:                        return mach_error_string(r);
	}
}

vproc_err_t
vproc_swap_integer(vproc_t vp, vproc_gsk_t key, int64_t *inval, int64_t *outval)
{
	(void)vp;
	(void)key;
	(void)inval;
	(void)outval;
	return (vproc_err_t)vproc_swap_integer;
}

static _Atomic size_t vproc_transactions;
static bool vproc_transactions_enabled;

void
_vproc_transaction_begin(void)
{
	atomic_fetch_add(&vproc_transactions, 1);
}

void
_vproc_transaction_end(void)
{
	size_t n = atomic_load(&vproc_transactions);
	while (n > 0 && !atomic_compare_exchange_weak(&vproc_transactions, &n, n - 1)) {
	}
}

size_t
_vproc_transaction_count(void)
{
	return atomic_load(&vproc_transactions);
}

void
_vproc_transactions_enable(void)
{
	vproc_transactions_enabled = true;
}

void
_vproc_transaction_try_exit(int status)
{
	if (vproc_transactions_enabled && atomic_load(&vproc_transactions) == 0) {
		exit(status);
	}
}

vproc_err_t
_vprocmgr_detach_from_console(vproc_flags_t flags)
{
	(void)flags;
	return NULL;
}

int
reboot3(uint64_t howto, ...)
{
	if (geteuid() != 0) {
		return EPERM;
	}
	kern_return_t kr = bs_rpc(bootstrap_port, PIE_BS_REBOOT, NULL, MACH_PORT_NULL, howto, NULL);
	return kr == KERN_SUCCESS ? 0 : EIO;
}

void *
reboot2(uint64_t howto)
{
	return reboot3(howto) == 0 ? NULL : (void *)1;
}
