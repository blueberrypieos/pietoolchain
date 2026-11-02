/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
/* Minimal host-side stand-in for <mach/ndr.h> (see message.h). */
#ifndef _PIE_HOST_MACH_NDR_H_
#define _PIE_HOST_MACH_NDR_H_
typedef struct {
	unsigned char mig_vers;
	unsigned char if_vers;
	unsigned char reserved1;
	unsigned char mig_encoding;
	unsigned char int_rep;
	unsigned char char_rep;
	unsigned char float_rep;
	unsigned char reserved2;
} NDR_record_t;
extern NDR_record_t NDR_record;
#endif
