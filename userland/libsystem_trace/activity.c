/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <os/activity.h>
#include <limits.h>
#include <stddef.h>

struct os_activity_s {
	const void *isa;
	int ref_cnt;
	int xref_cnt;
};

struct os_activity_s _os_activity_none = { NULL, INT_MAX, INT_MAX };
struct os_activity_s _os_activity_current = { NULL, INT_MAX, INT_MAX };

os_activity_t
_os_activity_create(void *dso, const char *description, os_activity_t activity,
    os_activity_flag_t flags)
{
	(void)dso; (void)description; (void)activity; (void)flags;
	return OS_ACTIVITY_NONE;
}

void
os_activity_apply(os_activity_t activity, void (^block)(void))
{
	(void)activity;
	block();
}

void
os_activity_apply_f(os_activity_t activity, void *context, void (*function)(void *))
{
	(void)activity;
	function(context);
}

void
os_activity_scope_enter(os_activity_t activity, os_activity_scope_state_t state)
{
	(void)activity;
	state->opaque[0] = state->opaque[1] = 0;
}

void
os_activity_scope_leave(os_activity_scope_state_t state)
{
	(void)state;
}

os_activity_id_t
os_activity_get_identifier(os_activity_t activity, os_activity_id_t *parent_id)
{
	(void)activity;
	if (parent_id != NULL) {
		*parent_id = 0;
	}
	return 0;
}

os_activity_id_t
os_activity_get_active(os_activity_id_t *entries, unsigned int *count)
{
	(void)entries;
	if (count != NULL) {
		*count = 0;
	}
	return 0;
}

void
_os_activity_label_useraction(void *dso, const char *name)
{
	(void)dso; (void)name;
}

void
_os_activity_initiate(void *dso, const char *description, os_activity_flag_t flags,
    void (^activity_block)(void))
{
	(void)dso; (void)description; (void)flags;
	activity_block();
}

void
_os_activity_initiate_f(void *dso, const char *description, os_activity_flag_t flags,
    void *context, void (*function)(void *))
{
	(void)dso; (void)description; (void)flags;
	function(context);
}

os_activity_t
_os_activity_start(void *dso, const char *description, os_activity_flag_t flags)
{
	(void)dso; (void)description; (void)flags;
	return OS_ACTIVITY_NONE;
}

void
os_activity_end(os_activity_t activity)
{
	(void)activity;
}
