/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef OS_THREAD_SELF_RESTRICT_H
#define OS_THREAD_SELF_RESTRICT_H

#include <stdbool.h>

#define OS_THREAD_SELF_RESTRICT_INLINE static inline __attribute__((__always_inline__))

OS_THREAD_SELF_RESTRICT_INLINE bool
os_thread_self_restrict_rwx_is_supported(void)
{
	return false;
}

OS_THREAD_SELF_RESTRICT_INLINE void
os_thread_self_restrict_rwx_to_rw(void)
{
}

OS_THREAD_SELF_RESTRICT_INLINE void
os_thread_self_restrict_rwx_to_rx(void)
{
}

OS_THREAD_SELF_RESTRICT_INLINE bool
os_thread_self_restrict_tpro_is_supported(void)
{
	return false;
}

OS_THREAD_SELF_RESTRICT_INLINE bool
os_thread_self_restrict_tpro_is_writable(void)
{
	return true;
}

OS_THREAD_SELF_RESTRICT_INLINE void
os_thread_self_restrict_tpro_to_rw(void)
{
}

OS_THREAD_SELF_RESTRICT_INLINE void
os_thread_self_restrict_tpro_to_ro(void)
{
}

#undef OS_THREAD_SELF_RESTRICT_INLINE

#endif
