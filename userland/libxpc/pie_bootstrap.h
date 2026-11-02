/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef _PIE_BOOTSTRAP_H_
#define _PIE_BOOTSTRAP_H_

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdint.h>

#define PIE_BS_ID_BASE   0x786e6900
#define PIE_BS_CHECK_IN  (PIE_BS_ID_BASE + 1)
#define PIE_BS_LOOK_UP   (PIE_BS_ID_BASE + 2)
#define PIE_BS_REGISTER  (PIE_BS_ID_BASE + 3)
#define PIE_BS_REBOOT    (PIE_BS_ID_BASE + 4)
#define PIE_BS_REPLY     100

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t port;
	uint64_t arg;
	name_t name;
} pie_bs_request_t;

typedef struct {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_port_descriptor_t port;
	kern_return_t ret;
} pie_bs_reply_t;

typedef struct {
	pie_bs_request_t req;
	mach_msg_max_trailer_t trailer;
} pie_bs_request_buffer_t;

typedef struct {
	pie_bs_reply_t reply;
	mach_msg_max_trailer_t trailer;
} pie_bs_reply_buffer_t;

#endif
