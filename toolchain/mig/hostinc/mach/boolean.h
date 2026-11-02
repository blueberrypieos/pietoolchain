/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Minimal host-side stand-in for <mach/boolean.h> (see message.h). */
#ifndef _PIE_HOST_MACH_BOOLEAN_H_
#define _PIE_HOST_MACH_BOOLEAN_H_
typedef int boolean_t;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif
