/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_DISPATCH_H
#define PIE_HOST_DISPATCH_H

#include <mach/mach.h>

typedef struct dispatch_queue_s *dispatch_queue_t;
typedef struct dispatch_queue_s *dispatch_source_t;
typedef void (^dispatch_block_t)(void);
typedef const void *dispatch_source_type_t;

#define DISPATCH_SOURCE_TYPE_MACH_RECV ((dispatch_source_type_t)"mach_recv")

dispatch_queue_t dispatch_queue_create(const char *label, const void *attr);
void dispatch_retain(void *object);
void dispatch_release(void *object);
void dispatch_suspend(void *object);
void dispatch_resume(void *object);
void dispatch_set_target_queue(void *object, dispatch_queue_t queue);
void dispatch_async(dispatch_queue_t queue, dispatch_block_t block);
dispatch_source_t dispatch_source_create(dispatch_source_type_t type, uintptr_t handle,
    uintptr_t mask, dispatch_queue_t queue);
void dispatch_source_set_event_handler(dispatch_source_t source, dispatch_block_t handler);
void dispatch_source_set_cancel_handler(dispatch_source_t source, dispatch_block_t handler);
void dispatch_source_cancel(dispatch_source_t source);

#endif
