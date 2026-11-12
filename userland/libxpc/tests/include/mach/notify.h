/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_MACH_NOTIFY_H
#define PIE_HOST_MACH_NOTIFY_H

#include <mach/mach.h>

#define MACH_NOTIFY_PORT_DELETED   0101
#define MACH_NOTIFY_SEND_POSSIBLE  0102
#define MACH_NOTIFY_NO_SENDERS     0106
#define MACH_NOTIFY_SEND_ONCE      0107
#define MACH_NOTIFY_DEAD_NAME      0110

typedef struct {
	unsigned char mig_vers, if_vers, reserved1, mig_encoding;
	unsigned char int_rep, char_rep, float_rep, reserved2;
} NDR_record_t;

typedef struct {
	mach_msg_header_t not_header;
	NDR_record_t NDR;
	mach_port_name_t not_port;
	mach_msg_audit_trailer_t trailer;
} mach_dead_name_notification_t;

typedef mach_dead_name_notification_t mach_send_possible_notification_t;

#endif
