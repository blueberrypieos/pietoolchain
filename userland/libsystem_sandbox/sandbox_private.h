/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _SANDBOX_PRIVATE_H_
#define _SANDBOX_PRIVATE_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdint.h>
#include <mach/message.h>

enum sandbox_filter_type {
	SANDBOX_FILTER_NONE           = 0,
	SANDBOX_FILTER_PATH           = 1,
	SANDBOX_FILTER_GLOBAL_NAME    = 2,
	SANDBOX_FILTER_LOCAL_NAME     = 3,
	SANDBOX_FILTER_APPLEEVENT_DESTINATION = 4,
	SANDBOX_FILTER_RIGHT_NAME     = 5,
	SANDBOX_FILTER_PREFERENCE_DOMAIN = 6,
	SANDBOX_FILTER_KEXT_BUNDLE_ID = 7,
	SANDBOX_FILTER_INFO_TYPE      = 8,
	SANDBOX_FILTER_NOTIFICATION   = 9,
	SANDBOX_FILTER_SYSCALL_NUMBER = 10,
};

#define SANDBOX_CHECK_NO_REPORT       0x40000000
#define SANDBOX_CHECK_CANONICAL       0x20000000

__BEGIN_DECLS
int sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...);
int sandbox_check_by_audit_token(audit_token_t token, const char *operation,
    enum sandbox_filter_type type, ...);
int sandbox_init_with_parameters(const char *profile, uint64_t flags,
    const char *const parameters[], char **errorbuf);
__END_DECLS

#endif
