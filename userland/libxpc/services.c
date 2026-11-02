/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <Block.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdlib.h>
#include <xpc/private.h>

int
xpc_event_publisher_fire_noboost(xpc_event_publisher_t publisher, uint64_t token,
    xpc_object_t details)
{
	(void)publisher; (void)token; (void)details;
	return ENOTSUP;
}

bool
xpc_get_service_identifier_for_token(uint64_t token, event_name_t name)
{
	(void)token;
	name[0] = '\0';
	return false;
}

xpc_object_t
xpc_copy_entitlement_for_token(const char *entitlement, audit_token_t *token)
{
	(void)entitlement; (void)token;
	return NULL;
}

bool
_xpc_runtime_is_app_sandboxed(void)
{
	return false;
}

struct _xpc_event_publisher_s {
	dispatch_queue_t queue;
	xpc_event_publisher_handler_t handler;
	xpc_event_publisher_error_handler_t error_handler;
};

xpc_event_publisher_t
xpc_event_publisher_create(const char *name, dispatch_queue_t queue)
{
	(void)name;
	struct _xpc_event_publisher_s *p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return NULL;
	}
	p->queue = queue != NULL ? queue : dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0);
	dispatch_retain(p->queue);
	return p;
}

void
xpc_event_publisher_set_handler(xpc_event_publisher_t p, xpc_event_publisher_handler_t handler)
{
	if (p->handler != NULL) {
		Block_release(p->handler);
	}
	p->handler = handler != NULL ? Block_copy(handler) : NULL;
}

void
xpc_event_publisher_set_error_handler(xpc_event_publisher_t p, xpc_event_publisher_error_handler_t handler)
{
	if (p->error_handler != NULL) {
		Block_release(p->error_handler);
	}
	p->error_handler = handler != NULL ? Block_copy(handler) : NULL;
}

void
xpc_event_publisher_set_throttling(xpc_event_publisher_t p, uint32_t limit)
{
	(void)p; (void)limit;
}

void
xpc_event_publisher_activate(xpc_event_publisher_t p)
{
	xpc_event_publisher_handler_t handler = p->handler;
	if (handler != NULL) {
		dispatch_async(p->queue, ^{
			handler(XPC_EVENT_PUBLISHER_ACTION_INITIAL_BARRIER, 0, NULL);
		});
	}
}

au_asid_t
xpc_event_publisher_get_subscriber_asid(xpc_event_publisher_t p, uint64_t token)
{
	(void)p; (void)token;
	return AU_DEFAUDITSID;
}
