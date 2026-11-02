/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#include "asmdefs.h"

ENTRY (_platform_bzero)
	mov	x2, x1
	mov	w1, 0
	b	__platform_memset
END (_platform_bzero)
