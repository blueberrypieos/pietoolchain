/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __XPC_PRIVATE_H__
#define __XPC_PRIVATE_H__

#include <xpc/xpc.h>
#include <errno.h>
#include <bsm/audit.h>

__BEGIN_DECLS

XPC_EXPORT xpc_object_t xpc_create_from_plist(const void *data, size_t length);

typedef struct _xpc_pipe_s *xpc_pipe_t;

#define XPC_PIPE_PRIVILEGED            (1 << 0)
#define XPC_PIPE_USE_SYNC_IPC_OVERRIDE (1 << 1)
#define XPC_PIPE_PROPAGATE_QOS         (1 << 2)

XPC_EXPORT xpc_pipe_t xpc_pipe_create(const char *name, uint64_t flags);
XPC_EXPORT xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags);
XPC_EXPORT int xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t message, xpc_object_t *reply);
XPC_EXPORT int xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t message,
    xpc_object_t *reply, uint64_t flags);
XPC_EXPORT int xpc_pipe_simpleroutine(xpc_pipe_t pipe, xpc_object_t message);
XPC_EXPORT void xpc_pipe_invalidate(xpc_pipe_t pipe);

/*
 * xpc_pipe_receive waits for a message on port. xpc_pipe_try_receive returns
 * EAGAIN at once if none is queued. It passes other Mach messages to demux,
 * sends demux's reply and returns 0 with *message NULL. A message that wants
 * a reply takes one from xpc_dictionary_create_reply and xpc_pipe_routine_reply.
 */
XPC_EXPORT int xpc_pipe_receive(mach_port_t port, xpc_object_t *message);
XPC_EXPORT int xpc_pipe_try_receive(mach_port_t port, xpc_object_t *message,
    mach_port_t *recvp, boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t msgsize, uint64_t flags);
XPC_EXPORT int xpc_pipe_routine_reply(xpc_object_t reply);

XPC_EXPORT void xpc_dictionary_get_audit_token(xpc_object_t xdict, audit_token_t *token);
XPC_EXPORT void xpc_connection_get_audit_token(xpc_connection_t connection,
    audit_token_t *token);
XPC_EXPORT void xpc_connection_set_legacy(xpc_connection_t connection);
XPC_EXPORT void xpc_connection_set_privileged(xpc_connection_t connection);

XPC_EXPORT XPC_TYPE(_xpc_type_mach_send);
#define XPC_TYPE_MACH_SEND (&_xpc_type_mach_send)
XPC_EXPORT xpc_object_t xpc_mach_send_create(mach_port_t send);
XPC_EXPORT mach_port_t xpc_mach_send_copy_right(xpc_object_t xsend);
XPC_EXPORT void xpc_dictionary_set_mach_send(xpc_object_t xdict, const char *key,
    mach_port_t send);
XPC_EXPORT mach_port_t xpc_dictionary_copy_mach_send(xpc_object_t xdict, const char *key);

typedef struct _xpc_event_publisher_s *xpc_event_publisher_t;
typedef char event_name_t[128];

XPC_EXPORT int xpc_event_publisher_fire_noboost(xpc_event_publisher_t publisher,
    uint64_t token, xpc_object_t details);
XPC_EXPORT bool xpc_get_service_identifier_for_token(uint64_t token, event_name_t name);

typedef enum {
	XPC_EVENT_PUBLISHER_ACTION_ADD = 0,
	XPC_EVENT_PUBLISHER_ACTION_REMOVE = 1,
	XPC_EVENT_PUBLISHER_ACTION_INITIAL_BARRIER = 2,
} xpc_event_publisher_action_t;
typedef void (^xpc_event_publisher_handler_t)(xpc_event_publisher_action_t action,
    uint64_t event_token, xpc_object_t descriptor);
typedef void (^xpc_event_publisher_error_handler_t)(int error);

XPC_EXPORT xpc_event_publisher_t xpc_event_publisher_create(const char *name, dispatch_queue_t queue);
XPC_EXPORT void xpc_event_publisher_set_handler(xpc_event_publisher_t publisher,
    xpc_event_publisher_handler_t handler);
XPC_EXPORT void xpc_event_publisher_set_error_handler(xpc_event_publisher_t publisher,
    xpc_event_publisher_error_handler_t handler);
XPC_EXPORT void xpc_event_publisher_set_throttling(xpc_event_publisher_t publisher, uint32_t limit);
XPC_EXPORT void xpc_event_publisher_activate(xpc_event_publisher_t publisher);
XPC_EXPORT au_asid_t xpc_event_publisher_get_subscriber_asid(xpc_event_publisher_t publisher,
    uint64_t token);

XPC_EXPORT xpc_object_t xpc_copy_entitlement_for_token(const char *entitlement,
    audit_token_t *token);

XPC_EXPORT bool _xpc_runtime_is_app_sandboxed(void);

XPC_EXPORT void _libxpc_initializer(void);
XPC_EXPORT void xpc_atfork_prepare(void);
XPC_EXPORT void xpc_atfork_parent(void);
XPC_EXPORT void xpc_atfork_child(void);

__END_DECLS

#endif
