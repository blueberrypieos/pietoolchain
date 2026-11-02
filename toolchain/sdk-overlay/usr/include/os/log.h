/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __os_log_h
#define __os_log_h

#include <Availability.h>
#include <os/availability.h>
#include <os/base.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

typedef struct os_log_s *os_log_t;

OS_ENUM(os_log_type, uint8_t,
    OS_LOG_TYPE_DEFAULT = 0x00,
    OS_LOG_TYPE_INFO    = 0x01,
    OS_LOG_TYPE_DEBUG   = 0x02,
    OS_LOG_TYPE_ERROR   = 0x10,
    OS_LOG_TYPE_FAULT   = 0x11);

extern struct os_log_s _os_log_default;
extern struct os_log_s _os_log_disabled;
#define OS_LOG_DEFAULT  (&_os_log_default)
#define OS_LOG_DISABLED (&_os_log_disabled)

#define OS_LOG_OBJECT_USE_OBJC 0

os_log_t os_log_create(const char *subsystem, const char *category);
bool os_log_type_enabled(os_log_t oslog, os_log_type_t type);

extern void *__dso_handle;

void _os_log_impl(void *dso, os_log_t log, os_log_type_t type,
    const char *format, uint8_t *buf, uint32_t size);
void _os_log_debug_impl(void *dso, os_log_t log, os_log_type_t type,
    const char *format, uint8_t *buf, uint32_t size);
void _os_log_error_impl(void *dso, os_log_t log, os_log_type_t type,
    const char *format, uint8_t *buf, uint32_t size);
void _os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type,
    const char *format, uint8_t *buf, uint32_t size);

#define OS_LOG_STRING(var, str) \
	__attribute__((section("__TEXT,__oslogstring,cstring_literals"))) \
	static const char var[] = str

#define __os_log_call(impl, log, type, format, ...) __extension__({ \
	os_log_t _log_tmp = (log); \
	os_log_type_t _type_tmp = (type); \
	if (os_log_type_enabled(_log_tmp, _type_tmp)) { \
	        OS_LOG_STRING(_os_fmt_str, format); \
	        uint8_t _os_fmt_buf[__builtin_os_log_format_buffer_size(format, ##__VA_ARGS__)]; \
	        impl(&__dso_handle, _log_tmp, _type_tmp, _os_fmt_str, \
	            (uint8_t *)__builtin_os_log_format(_os_fmt_buf, format, ##__VA_ARGS__), \
	            (uint32_t)sizeof(_os_fmt_buf)); \
	} \
})

#define os_log_with_type(log, type, format, ...) \
	__os_log_call(_os_log_impl, log, type, format, ##__VA_ARGS__)
#define os_log(log, format, ...) \
	os_log_with_type(log, OS_LOG_TYPE_DEFAULT, format, ##__VA_ARGS__)
#define os_log_info(log, format, ...) \
	os_log_with_type(log, OS_LOG_TYPE_INFO, format, ##__VA_ARGS__)
#define os_log_debug(log, format, ...) \
	__os_log_call(_os_log_debug_impl, log, OS_LOG_TYPE_DEBUG, format, ##__VA_ARGS__)
#define os_log_error(log, format, ...) \
	__os_log_call(_os_log_error_impl, log, OS_LOG_TYPE_ERROR, format, ##__VA_ARGS__)
#define os_log_fault(log, format, ...) \
	__os_log_call(_os_log_fault_impl, log, OS_LOG_TYPE_FAULT, format, ##__VA_ARGS__)

__END_DECLS

#endif
