/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <libamfi.h>
#include <sandbox/private.h>
#include <unistd.h>

#define ALL_DYLD_POLICY_FLAGS \
	(AMFI_DYLD_OUTPUT_ALLOW_AT_PATH | AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS | \
	 AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE | AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS | \
	 AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS | AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION | \
	 AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING | AMFI_DYLD_OUTPUT_ALLOW_EMBEDDED_VARS | \
	 AMFI_DYLD_OUTPUT_ALLOW_DEVELOPMENT_VARS | AMFI_DYLD_OUTPUT_ALLOW_LIBSYSTEM_OVERRIDE)

int
amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags)
{
	if (output_flags == NULL) {
		return -1;
	}
	if ((input_flags & AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG) || issetugid()) {
		*output_flags = 0;
	} else {
		*output_flags = ALL_DYLD_POLICY_FLAGS;
	}
	return 0;
}

int
sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...)
{
	(void)pid;
	(void)operation;
	(void)type;
	return 0;
}
