/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _LIBAMFI_H_
#define _LIBAMFI_H_

#include <stdint.h>
#include <sys/cdefs.h>

enum amfi_dyld_policy_input_flag_set {
	AMFI_DYLD_INPUT_PROC_IN_SIMULATOR     = (1 << 0),
	AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG = (1 << 1),
	AMFI_DYLD_INPUT_PROC_IS_ENCRYPTED     = (1 << 2),
};

enum amfi_dyld_policy_output_flag_set {
	AMFI_DYLD_OUTPUT_ALLOW_AT_PATH                  = (1 << 0),
	AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS                = (1 << 1),
	AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE      = (1 << 2),
	AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS           = (1 << 3),
	AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS               = (1 << 4),
	AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION = (1 << 5),
	AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING      = (1 << 6),
	AMFI_DYLD_OUTPUT_ALLOW_EMBEDDED_VARS            = (1 << 7),
	AMFI_DYLD_OUTPUT_ALLOW_DEVELOPMENT_VARS         = (1 << 8),
	AMFI_DYLD_OUTPUT_ALLOW_LIBSYSTEM_OVERRIDE       = (1 << 9),
};

__BEGIN_DECLS
int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags);
__END_DECLS

#endif
