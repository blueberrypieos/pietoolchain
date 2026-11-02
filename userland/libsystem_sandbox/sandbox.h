/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _SANDBOX_H_
#define _SANDBOX_H_

#include <sys/cdefs.h>
#include <stdint.h>

__BEGIN_DECLS

#define SANDBOX_NAMED   0x0001

extern const char kSBXProfileNoInternet[];
extern const char kSBXProfileNoNetwork[];
extern const char kSBXProfileNoWrite[];
extern const char kSBXProfileNoWriteExceptTemporary[];
extern const char kSBXProfilePureComputation[];

int sandbox_init(const char *profile, uint64_t flags, char **errorbuf);
void sandbox_free_error(char *errorbuf);

__END_DECLS

#if __has_include(<sandbox/private.h>)
#include <sandbox/private.h>
#endif

#endif
