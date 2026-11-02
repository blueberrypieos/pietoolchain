/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __OS_TRACE_PRIVATE_H__
#define __OS_TRACE_PRIVATE_H__

#include <sys/cdefs.h>
#include <stdint.h>

__BEGIN_DECLS

typedef uint32_t os_trace_mode_t;
#define OS_TRACE_MODE_INFO              0x0001
#define OS_TRACE_MODE_DEBUG             0x0002
#define OS_TRACE_MODE_BACKTRACE         0x0004
#define OS_TRACE_MODE_STREAM_LIVE       0x0008
#define OS_TRACE_MODE_DISABLE           0x0100
#define OS_TRACE_MODE_OFF               0x0400

void os_trace_set_mode(os_trace_mode_t mode);
os_trace_mode_t os_trace_get_mode(void);

__END_DECLS

#endif
