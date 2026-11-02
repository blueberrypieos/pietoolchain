/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_CFURLENUMERATOR_H
#define PIE_CFURLENUMERATOR_H
#include <CoreFoundation/CFBase.h>
typedef const struct __CFURLEnumerator *CFURLEnumeratorRef;
typedef CF_ENUM(CFIndex, CFURLEnumeratorResult) {
	kCFURLEnumeratorSuccess = 1,
	kCFURLEnumeratorEnd = 2,
	kCFURLEnumeratorError = 3,
	kCFURLEnumeratorDirectoryPostOrderSuccess = 4,
};
#endif
