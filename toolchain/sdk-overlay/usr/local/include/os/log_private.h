/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __os_log_private_h
#define __os_log_private_h

#include <os/log.h>
#include <mach/mach_types.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <uuid/uuid.h>

__BEGIN_DECLS

typedef struct os_log_pack_s {
	uint64_t        olp_continuous_time;
	struct timespec olp_wall_time;
	const void     *olp_mh;
	const void     *olp_pc;
	const char     *olp_format;
	uint8_t         olp_data[0];
} os_log_pack_s, *os_log_pack_t;

char *os_log_pack_send_and_compose(os_log_pack_t pack, os_log_t log,
    os_log_type_t type, char *buf, size_t size);

OS_OPTIONS(os_log_send_and_compose_flags, uint32_t,
	OS_LOG_F_SEND    = 0x1,
	OS_LOG_F_COMPOSE = 0x2,
);

char *os_log_send_and_compose(os_log_send_and_compose_flags_t flags,
    const char **fmt_out, char *buf, size_t size, os_log_t log,
    os_log_type_t type, const char *fmt, ...);

bool os_log_shim_enabled(void *addr);

void os_log_with_args(os_log_t log, os_log_type_t type, const char *format,
    va_list args, void *ret_addr);
void os_log_with_args_4syslog(os_log_t log, os_log_type_t type, const char *format,
    va_list args, void *ret_addr);

__END_DECLS

#endif
