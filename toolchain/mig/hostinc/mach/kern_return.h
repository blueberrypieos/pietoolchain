/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Minimal host-side stand-in for <mach/kern_return.h> (see message.h). */
#ifndef _PIE_HOST_MACH_KERN_RETURN_H_
#define _PIE_HOST_MACH_KERN_RETURN_H_
typedef int kern_return_t;
#define KERN_SUCCESS 0
#endif
