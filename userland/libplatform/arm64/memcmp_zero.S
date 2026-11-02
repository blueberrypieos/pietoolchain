/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#include "asmdefs.h"

#define src	x0
#define cnt	x1
#define end	x2
#define t	x3

ENTRY (_platform_memcmp_zero_aligned8)
	add	end, src, cnt
	cmp	cnt, 16
	b.hi	L(gt16)
	cbz	cnt, L(zero)
	ldr	t, [src]
	ldr	x4, [end, -8]
	orr	t, t, x4
	b	L(done)

L(gt16):
	cmp	cnt, 32
	b.hi	L(gt32)
	ldr	q0, [src]
	ldr	q1, [end, -16]
	orr	v0.16b, v0.16b, v1.16b
	b	L(reduce)

L(gt32):
	cmp	cnt, 64
	b.hi	L(loop64)
	ldp	q0, q1, [src]
	ldp	q2, q3, [end, -32]
	b	L(or4)

L(loop64):
	ldp	q0, q1, [src]
	ldp	q2, q3, [src, 32]
	add	src, src, 64
	sub	cnt, cnt, 64
	orr	v0.16b, v0.16b, v1.16b
	orr	v2.16b, v2.16b, v3.16b
	orr	v0.16b, v0.16b, v2.16b
	umaxp	v0.4s, v0.4s, v0.4s
	fmov	t, d0
	cbnz	t, L(done)
	cmp	cnt, 64
	b.hi	L(loop64)
	ldp	q0, q1, [end, -64]
	ldp	q2, q3, [end, -32]
L(or4):
	orr	v0.16b, v0.16b, v1.16b
	orr	v2.16b, v2.16b, v3.16b
	orr	v0.16b, v0.16b, v2.16b
L(reduce):
	umaxp	v0.4s, v0.4s, v0.4s
	fmov	t, d0
L(done):
	cmp	t, 0
	cset	x0, ne
	ret
L(zero):
	mov	x0, 0
	ret
END (_platform_memcmp_zero_aligned8)
