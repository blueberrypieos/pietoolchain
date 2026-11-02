/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __os_activity_h
#define __os_activity_h

#include <os/base.h>
#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

typedef struct os_activity_s *os_activity_t;
typedef uint64_t os_activity_id_t;

OS_ENUM(os_activity_flag, uint32_t,
    OS_ACTIVITY_FLAG_DEFAULT = 0,
    OS_ACTIVITY_FLAG_DETACHED = 0x1,
    OS_ACTIVITY_FLAG_IF_NONE_PRESENT = 0x2);

extern struct os_activity_s _os_activity_none;
extern struct os_activity_s _os_activity_current;
#define OS_ACTIVITY_NONE    (&_os_activity_none)
#define OS_ACTIVITY_CURRENT (&_os_activity_current)

extern void *__dso_handle;

os_activity_t _os_activity_create(void *dso, const char *description,
    os_activity_t activity, os_activity_flag_t flags);
#define os_activity_create(description, activity, flags) \
	_os_activity_create(&__dso_handle, (description), (activity), (flags))

#ifdef __BLOCKS__
void os_activity_apply(os_activity_t activity, void (^block)(void));
#endif
void os_activity_apply_f(os_activity_t activity, void *context, void (*function)(void *));

typedef struct os_activity_scope_state_s {
	uint64_t opaque[2];
} *os_activity_scope_state_t;

void os_activity_scope_enter(os_activity_t activity, os_activity_scope_state_t state);
void os_activity_scope_leave(os_activity_scope_state_t state);

#define os_activity_scope(activity) \
	struct os_activity_scope_state_s _os_activity_scope_state \
	    __attribute__((__cleanup__(os_activity_scope_leave))); \
	os_activity_scope_enter((activity), &_os_activity_scope_state)

os_activity_id_t os_activity_get_identifier(os_activity_t activity, os_activity_id_t *parent_id);
os_activity_id_t os_activity_get_active(os_activity_id_t *entries, unsigned int *count);

void _os_activity_label_useraction(void *dso, const char *name);
#define os_activity_label_useraction(label) \
	_os_activity_label_useraction(&__dso_handle, (label))

#ifdef __BLOCKS__
void _os_activity_initiate(void *dso, const char *description,
    os_activity_flag_t flags, void (^activity_block)(void));
#define os_activity_initiate(description, flags, activity_block) \
	_os_activity_initiate(&__dso_handle, (description), (flags), (activity_block))
#endif
void _os_activity_initiate_f(void *dso, const char *description,
    os_activity_flag_t flags, void *context, void (*function)(void *));
#define os_activity_initiate_f(description, flags, context, function) \
	_os_activity_initiate_f(&__dso_handle, (description), (flags), (context), (function))
os_activity_t _os_activity_start(void *dso, const char *description, os_activity_flag_t flags);
#define os_activity_start(description, flags) \
	_os_activity_start(&__dso_handle, (description), (flags))
void os_activity_end(os_activity_t activity);

__END_DECLS

#endif
