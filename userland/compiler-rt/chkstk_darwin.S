/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

	.text
	.globl	___chkstk_darwin
	.p2align 2
___chkstk_darwin:
	mov	x16, x9
	mov	x17, sp
1:	cmp	x16, #0x1000
	b.lo	2f
	sub	x17, x17, #0x1000
	ldr	xzr, [x17]
	sub	x16, x16, #0x1000
	b	1b
2:	sub	x17, x17, x16
	ldr	xzr, [x17]
	ret
