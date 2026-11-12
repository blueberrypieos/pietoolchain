/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _ROOTLESS_H_
#define _ROOTLESS_H_

#include <sys/cdefs.h>

__BEGIN_DECLS

int rootless_restricted_environment(void);
int rootless_check_trusted_fd(int fd);

__END_DECLS

#endif
