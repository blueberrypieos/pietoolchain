/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */

#include "asmdefs.h"

#define dst	x0
#define pat	x1
#define len	x2
#define n	x3

ENTRY (_platform_memset_pattern4)
	ld1r	{v0.4s}, [pat]
	b	L(fill)
END (_platform_memset_pattern4)

ENTRY (_platform_memset_pattern8)
	ld1r	{v0.2d}, [pat]
	b	L(fill)
END (_platform_memset_pattern8)

ENTRY (_platform_memset_pattern16)
	ldr	q0, [pat]
L(fill):
	lsr	n, len, 6
	cbz	n, L(lt64)
L(loop64):
	stp	q0, q0, [dst]
	stp	q0, q0, [dst, 32]
	add	dst, dst, 64
	subs	n, n, 1
	b.ne	L(loop64)
L(lt64):
	tbz	len, 5, 1f
	stp	q0, q0, [dst], 32
1:	tbz	len, 4, 1f
	str	q0, [dst], 16
1:	tbz	len, 3, 1f
	str	d0, [dst], 8
	ext	v0.16b, v0.16b, v0.16b, 8
1:	tbz	len, 2, 1f
	str	s0, [dst], 4
	ext	v0.16b, v0.16b, v0.16b, 4
1:	tbz	len, 1, 1f
	str	h0, [dst], 2
	ext	v0.16b, v0.16b, v0.16b, 2
1:	tbz	len, 0, 1f
	str	b0, [dst]
1:	ret
END (_platform_memset_pattern16)
