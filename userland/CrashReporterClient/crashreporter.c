/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <CrashReporterClient.h>

CRASH_REPORTER_CLIENT_HIDDEN
struct crashreporter_annotations_t gCRAnnotations
__attribute__((section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION))) = {
	.version = CRASHREPORTER_ANNOTATIONS_VERSION,
};
