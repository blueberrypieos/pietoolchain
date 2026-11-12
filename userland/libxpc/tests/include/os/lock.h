/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_HOST_OS_LOCK_H
#define PIE_HOST_OS_LOCK_H

#include <stdint.h>

typedef struct {
	uint32_t owned;
} os_unfair_lock;

#define OS_UNFAIR_LOCK_INIT ((os_unfair_lock){ 0 })

void os_unfair_lock_lock(os_unfair_lock *lock);
void os_unfair_lock_unlock(os_unfair_lock *lock);

#endif
