/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __OS_ALLOC_ONCE_PRIVATE__
#define __OS_ALLOC_ONCE_PRIVATE__

#define __OS_ALLOC_INDIRECT__
#include <os/alloc_once_impl.h>

#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY          0
#define OS_ALLOC_ONCE_KEY_LIBXPC                    1
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_C               2
#define OS_ALLOC_ONCE_KEY_LIBKEYMGR                 3
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_INFO            4
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NETWORK         5
#define OS_ALLOC_ONCE_KEY_LIBCACHE                  6
#define OS_ALLOC_ONCE_KEY_LIBCOMMONCRYPTO           7
#define OS_ALLOC_ONCE_KEY_LIBDISPATCH               8
#define OS_ALLOC_ONCE_KEY_LIBDYLD                   9
#define OS_ALLOC_ONCE_KEY_OS_TRACE                  10
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_PLATFORM_ASL    11
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD         12
#define OS_ALLOC_ONCE_KEY_OS_DEBUG_LOG              13
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_ASL             14
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_DNSSD           15
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_BLOCKS          16
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_MALLOC          17
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_TRACE           18

#endif
