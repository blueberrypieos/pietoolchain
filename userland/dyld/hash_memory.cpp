/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <functional>

namespace std {
inline namespace __1 {

size_t __hash_memory(__attribute__((noescape)) const void* ptr, size_t size) noexcept
{
	return __murmur2_or_cityhash<size_t>()(ptr, size);
}

}
}
