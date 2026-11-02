/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Minimal host-side stand-in for <mach/port.h> (see message.h). */
#ifndef _PIE_HOST_MACH_PORT_H_
#define _PIE_HOST_MACH_PORT_H_
#include <mach/boolean.h>
typedef unsigned int natural_t;
typedef int integer_t;
typedef natural_t mach_port_name_t;
typedef mach_port_name_t mach_port_t;
#define MACH_PORT_NULL 0
#endif
