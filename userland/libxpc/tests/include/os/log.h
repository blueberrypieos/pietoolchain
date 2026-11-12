/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_OS_LOG_H
#define PIE_HOST_OS_LOG_H

#define OS_LOG_DEFAULT ((void *)0)

void test_os_log(void *log, const char *format, ...);

#define os_log_error(log, format, ...) test_os_log(log, format, ##__VA_ARGS__)
#define os_log_fault(log, format, ...) test_os_log(log, format, ##__VA_ARGS__)

#endif
