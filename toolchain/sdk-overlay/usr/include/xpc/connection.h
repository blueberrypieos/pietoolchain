/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __XPC_CONNECTION_H__
#define __XPC_CONNECTION_H__

#ifndef __XPC_H__
#error "Please #include <xpc/xpc.h> instead of this file directly."
#endif

#include <bsm/audit.h>
#include <dispatch/dispatch.h>

__BEGIN_DECLS

XPC_EXPORT const struct _xpc_dictionary_s _xpc_error_connection_interrupted;
XPC_EXPORT const struct _xpc_dictionary_s _xpc_error_connection_invalid;
XPC_EXPORT const struct _xpc_dictionary_s _xpc_error_termination_imminent;
#define XPC_ERROR_CONNECTION_INTERRUPTED ((xpc_object_t)&_xpc_error_connection_interrupted)
#define XPC_ERROR_CONNECTION_INVALID     ((xpc_object_t)&_xpc_error_connection_invalid)
#define XPC_ERROR_TERMINATION_IMMINENT   ((xpc_object_t)&_xpc_error_termination_imminent)

XPC_EXPORT const char *const _xpc_error_key_description;
#define XPC_ERROR_KEY_DESCRIPTION _xpc_error_key_description

#define XPC_CONNECTION_MACH_SERVICE_LISTENER   (1 << 0)
#define XPC_CONNECTION_MACH_SERVICE_PRIVILEGED (1 << 1)

XPC_EXPORT xpc_connection_t xpc_connection_create(const char *name, dispatch_queue_t targetq);
XPC_EXPORT xpc_connection_t xpc_connection_create_mach_service(const char *name,
    dispatch_queue_t targetq, uint64_t flags);
XPC_EXPORT xpc_connection_t xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint);
XPC_EXPORT void xpc_connection_set_target_queue(xpc_connection_t connection, dispatch_queue_t targetq);
#ifdef __BLOCKS__
XPC_EXPORT void xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler);
#endif
XPC_EXPORT void xpc_connection_activate(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_suspend(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_resume(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message);
#ifdef __BLOCKS__
XPC_EXPORT void xpc_connection_send_barrier(xpc_connection_t connection, dispatch_block_t barrier);
XPC_EXPORT void xpc_connection_send_message_with_reply(xpc_connection_t connection,
    xpc_object_t message, dispatch_queue_t replyq, xpc_handler_t handler);
#endif
XPC_EXPORT xpc_object_t xpc_connection_send_message_with_reply_sync(xpc_connection_t connection,
    xpc_object_t message);
XPC_EXPORT void xpc_connection_cancel(xpc_connection_t connection);
XPC_EXPORT const char *xpc_connection_get_name(xpc_connection_t connection);
XPC_EXPORT uid_t xpc_connection_get_euid(xpc_connection_t connection);
XPC_EXPORT gid_t xpc_connection_get_egid(xpc_connection_t connection);
XPC_EXPORT pid_t xpc_connection_get_pid(xpc_connection_t connection);
XPC_EXPORT au_asid_t xpc_connection_get_asid(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_set_context(xpc_connection_t connection, void *context);
XPC_EXPORT void *xpc_connection_get_context(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_set_finalizer_f(xpc_connection_t connection, xpc_finalizer_t finalizer);

__END_DECLS

#endif
