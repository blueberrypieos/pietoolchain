/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_BLOCK_H
#define PIE_HOST_BLOCK_H

void *_Block_copy(const void *block);
void _Block_release(const void *block);

#define Block_copy(b) ((__typeof__(b))_Block_copy((const void *)(b)))
#define Block_release(b) _Block_release((const void *)(b))

#endif
