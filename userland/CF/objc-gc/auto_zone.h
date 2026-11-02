/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#ifndef PIE_AUTO_ZONE_H
#define PIE_AUTO_ZONE_H
#include <stdbool.h>
#include <stddef.h>
#include <malloc/malloc.h>
#include <sys/cdefs.h>

typedef malloc_zone_t auto_zone_t;
typedef unsigned int auto_memory_type_t;
enum {
	AUTO_TYPE_UNKNOWN = -1,
	AUTO_UNSCANNED = (1 << 0),
	AUTO_OBJECT = (1 << 1),
	AUTO_POINTERS_ONLY = (1 << 2),
	AUTO_MEMORY_SCANNED = !AUTO_UNSCANNED,
	AUTO_MEMORY_UNSCANNED = AUTO_UNSCANNED,
	AUTO_MEMORY_ALL_POINTERS = AUTO_POINTERS_ONLY,
	AUTO_MEMORY_ALL_WEAK_POINTERS = (AUTO_UNSCANNED | AUTO_POINTERS_ONLY),
	AUTO_OBJECT_SCANNED = AUTO_OBJECT,
	AUTO_OBJECT_UNSCANNED = AUTO_OBJECT | AUTO_UNSCANNED,
	AUTO_OBJECT_ALL_POINTERS = AUTO_OBJECT | AUTO_POINTERS_ONLY,
};

__BEGIN_DECLS
void *auto_zone_allocate_object(auto_zone_t *zone, size_t size, auto_memory_type_t type,
    bool initial_refcount_to_one, bool clear);
unsigned int auto_zone_retain(auto_zone_t *zone, void *ptr);
unsigned int auto_zone_release(auto_zone_t *zone, void *ptr);
void auto_zone_set_unscanned(auto_zone_t *zone, void *ptr);
void auto_zone_set_nofinalize(auto_zone_t *zone, void *ptr);
bool auto_zone_is_valid_pointer(auto_zone_t *zone, const void *ptr);
void *auto_zone_base_pointer(auto_zone_t *zone, const void *ptr);
__END_DECLS
#endif
