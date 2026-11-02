/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _CRASHREPORTERCLIENT_H_
#define _CRASHREPORTERCLIENT_H_

#include <stdint.h>
#include <sys/cdefs.h>

#define CRASHREPORTER_ANNOTATIONS_SECTION "__crash_info"
#define CRASHREPORTER_ANNOTATIONS_VERSION 5
#define CRASH_REPORTER_CLIENT_HIDDEN __attribute__((visibility("hidden")))

struct crashreporter_annotations_t {
	uint64_t version;
	uint64_t message;
	uint64_t signature_string;
	uint64_t backtrace;
	uint64_t message2;
	uint64_t thread;
	uint64_t dialog_mode;
	uint64_t abort_cause;
};

__BEGIN_DECLS
CRASH_REPORTER_CLIENT_HIDDEN
extern struct crashreporter_annotations_t gCRAnnotations;
__END_DECLS

#define CRGetCrashLogMessage()   ((const char *)(uintptr_t)gCRAnnotations.message)
#define CRSetCrashLogMessage(m)  ((void)(gCRAnnotations.message = (uint64_t)(uintptr_t)(m)))
#define CRGetCrashLogMessage2()  ((const char *)(uintptr_t)gCRAnnotations.message2)
#define CRSetCrashLogMessage2(m) ((void)(gCRAnnotations.message2 = (uint64_t)(uintptr_t)(m)))

#endif
