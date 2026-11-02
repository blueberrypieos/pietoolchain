/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <stdlib.h>
#include <sandbox.h>
#include <sandbox/private.h>
#include <rootless.h>

const char kSBXProfileNoInternet[] = "no-internet";
const char kSBXProfileNoNetwork[] = "no-network";
const char kSBXProfileNoWrite[] = "no-write";
const char kSBXProfileNoWriteExceptTemporary[] = "no-write-except-temporary";
const char kSBXProfilePureComputation[] = "pure-computation";

int
sandbox_init(const char *profile, uint64_t flags, char **errorbuf)
{
	(void)profile;
	(void)flags;
	if (errorbuf != NULL) {
		*errorbuf = NULL;
	}
	return 0;
}

int
sandbox_init_with_parameters(const char *profile, uint64_t flags, const char *const parameters[],
    char **errorbuf)
{
	(void)parameters;
	return sandbox_init(profile, flags, errorbuf);
}

void
sandbox_free_error(char *errorbuf)
{
	free(errorbuf);
}

int
sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
	(void)pid;
	(void)operation;
	(void)type;
	return 0;
}

int
sandbox_check_by_audit_token(audit_token_t token, const char *operation, enum sandbox_filter_type type, ...)
{
	(void)token;
	(void)operation;
	(void)type;
	return 0;
}

int
rootless_restricted_environment(void)
{
	return 0;
}
