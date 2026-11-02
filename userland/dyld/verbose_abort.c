/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern void abort_report_np(const char *fmt, ...) __attribute__((noreturn));

__attribute__((noreturn, visibility("hidden")))
void _libcpp_verbose_abort(const char *msg, ...) __asm("__ZNSt3__122__libcpp_verbose_abortEPKcz");

void
_libcpp_verbose_abort(const char *msg, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, msg);
	vsnprintf(buf, sizeof(buf), msg, ap);
	va_end(ap);
	abort_report_np("%s", buf);
}
