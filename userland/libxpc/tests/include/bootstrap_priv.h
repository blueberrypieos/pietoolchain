/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_BOOTSTRAP_PRIV_H
#define PIE_HOST_BOOTSTRAP_PRIV_H

#include <servers/bootstrap.h>

#define BOOTSTRAP_PRIVILEGED_SERVER (1 << 3)

kern_return_t bootstrap_look_up2(mach_port_t bp, const char *service_name, mach_port_t *sp,
    pid_t target_pid, uint64_t flags);

#endif
