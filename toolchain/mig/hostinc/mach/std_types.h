/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Minimal host-side stand-in for <mach/std_types.h> (see message.h). */
#ifndef _PIE_HOST_MACH_STD_TYPES_H_
#define _PIE_HOST_MACH_STD_TYPES_H_
#include <stdint.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/port.h>
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_size_t;
#endif
