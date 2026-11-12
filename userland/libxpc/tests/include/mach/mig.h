/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_MACH_MIG_H
#define PIE_HOST_MACH_MIG_H

#include <mach/mach.h>

#define MIG_BAD_ID       (-303)
#define MIG_NO_REPLY     (-305)
#define MAX_TRAILER_SIZE 68

typedef struct {
	mach_msg_header_t Head;
	uint8_t NDR[8];
	kern_return_t RetCode;
} mig_reply_error_t;

#define MACH_PORT_TYPE_RECEIVE  0x00020000u
#define MACH_PORT_TYPE_PORT_SET 0x00080000u

kern_return_t mach_port_type(mach_port_t task, mach_port_name_t name, mach_port_type_t *type);

#endif
