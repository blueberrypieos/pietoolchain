/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_SERVERS_BOOTSTRAP_H
#define PIE_HOST_SERVERS_BOOTSTRAP_H

#include <mach/mach.h>

extern mach_port_t bootstrap_port;

kern_return_t bootstrap_check_in(mach_port_t bp, const char *service_name, mach_port_t *sp);

#endif
