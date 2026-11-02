/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef __XPC_BASE_H__
#define __XPC_BASE_H__

#include <sys/cdefs.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XPC_EXPORT extern __attribute__((visibility("default")))
#define XPC_NONNULL1 __attribute__((__nonnull__(1)))
#define XPC_NONNULL2 __attribute__((__nonnull__(2)))
#define XPC_NONNULL_ALL __attribute__((__nonnull__))
#define XPC_WARN_RESULT __attribute__((__warn_unused_result__))
#define XPC_MALLOC __attribute__((__malloc__))
#define XPC_RETURNS_RETAINED
#define XPC_INLINE static __inline__ __attribute__((__always_inline__))

#define XPC_DECL(name) typedef struct _##name##_s *name##_t

#endif
