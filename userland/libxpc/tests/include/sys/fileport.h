/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_FILEPORT_H
#define PIE_HOST_FILEPORT_H

#include <mach/mach.h>

int fileport_makeport(int fd, mach_port_t *port);
int fileport_makefd(mach_port_t port);

#endif
